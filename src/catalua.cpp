#include "catalua.h"

#include <memory>

#include "action.h"
#include "cata_algo.h"
#include "debug.h"
#include "game.h"
#include "item.h"
#include "item_factory.h"
#include "iuse.h"
#include "map.h"
#include "mapgen.h"
#include "mapgen_functions.h"
#include "messages.h"
#include "monstergenerator.h"
#include "omdata.h"
#include "output.h"
#include "overmap.h"
#include "path_info.h"
#include "player.h"
#include "pldata.h"
#include "requirements.h"
#include "string_formatter.h"
#include "translations.h"
#include "weather_gen.h"

#ifdef LUA

// modernized: This seems to be a general lua wrapper for Cataclysm: Dark Days Ahead.
// Many pragmas here for headers which IWYU doesn't realise we need because
// they are used only inside src/lua/catabindings.cpp (the autogenerated file)

#include "activity_type.h" // IWYU pragma: keep
#include "bionics.h" // IWYU pragma: keep
#include "field.h" // IWYU pragma: keep
#include "avatar.h" // avatar for avatar retrieval
#include "filesystem.h"
#include "gun_mode.h"
#include "itype.h"
#include "line.h" // IWYU pragma: keep
#include "mapdata.h"
#include "mongroup.h"
#include "morale_types.h" // IWYU pragma: keep
#include "mtype.h"
#include "mutation.h" // IWYU pragma: keep
#include "npc.h" // IWYU pragma: keep
#include "optional.h"
#include "overmap.h"
#include "overmap_ui.h" // IWYU pragma: keep
#include "requirements.h" // IWYU pragma: keep
#include "rng.h" // IWYU pragma: keep
#include "string_input_popup.h"
#include "trap.h" // IWYU pragma: keep
#include "ui.h"

// modernization
// #include <lua.h>
// #include <lualib.h>
// #include <lauxlib.h>
// end modernization


extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <type_traits>

#if LUA_VERSION_NUM < 502
#define LUA_OK 0
#endif

using item_stack_iterator = std::list<item>::iterator;
using volume = units::volume;
using mass = units::mass;
using npc_template_id = string_id<npc_template>;
using overmap_direction = om_direction::type;

// lua_State *lua_state = nullptr;

// Keep track of the current mod from which we are executing, so that
// we know where to load files from.
std::string lua_file_path;

std::stringstream lua_output_stream;
std::stringstream lua_error_stream;

// Not used in the C++ code, but implicitly required by the Lua bindings.
// Gun modes need to be created via an actual item.
template<>
const gun_mode &string_id<gun_mode>::obj() const
{
    static const gun_mode dummy{};
    return dummy;
}
template<>
bool string_id<gun_mode>::is_valid() const
{
    return false;
}

#if LUA_VERSION_NUM < 502
// Compatibility, for before Lua 5.2, which does not have luaL_setfuncs
static void luaL_setfuncs( lua_State *const L, const luaL_Reg arrary[], const int nup )
{
    for( ; arrary->name != nullptr; arrary++ ) {
        lua_pushstring( L, arrary->name );
        // Need to copy the up-values because lua_pushcclosure removes them, they need
        // to be set for each C-function.
        for( int i = 0; i < nup; i++ ) {
            lua_pushvalue( L, -( nup + 1 ) );
        }
        lua_pushcclosure( L, arrary->func, nup );
        lua_settable( L, -( nup + 3 ) );
    }
    // Remove up-values as per definition of luaL_setfuncs in 5.2
    lua_pop( L, nup );
}
#endif

void lua_dofile( lua_State *L, const char *path );

// Helper functions for making working with the lua API more straightforward.
// --------------------------------------------------------------------------

// Stores item at the given stack position into the registry.
int luah_store_in_registry( lua_State *L, int stackpos )
{
    lua_pushvalue( L, stackpos );
    return luaL_ref( L, LUA_REGISTRYINDEX );
}

// Removes item from registry and pushes on the top of stack.
void luah_remove_from_registry( lua_State *L, int item_index )
{
    lua_rawgeti( L, LUA_REGISTRYINDEX, item_index );
    luaL_unref( L, LUA_REGISTRYINDEX, item_index );
}

// Sets the metatable for the element on top of the stack.
void luah_setmetatable( lua_State *L, const char *metatable_name )
{
    // Push the metatable on top of the stack.
    lua_getglobal( L, metatable_name );

    // The element we want to set the metatable for is now below the top.
    lua_setmetatable( L, -2 );
}

void luah_setglobal( lua_State *L, const char *name, int index )
{
    lua_pushvalue( L, index );
    lua_setglobal( L, name );
}

/** Safe wrapper to get a Lua string as std::string. Handles nullptr and binary data. */
std::string lua_tostring_wrapper( lua_State *const L, const int stack_position )
{
    size_t length = 0;
    const char *const result = lua_tolstring( L, stack_position, &length );
    if( result == nullptr || length == 0 ) {
        return std::string{};
    }
    return std::string( result, length );
}

// Given a Lua return code and a file that it happened in, print a debugmsg with the error and path.
// Returns true if there was an error, false if there was no error at all.
bool lua_report_error( lua_State *L, int err, const char *path, bool simple = false )
{
    if( err == LUA_OK || err == LUA_ERRRUN ) {
        // No error or error message already shown via traceback function.
        return err != LUA_OK;
    }
    const std::string error = lua_tostring_wrapper( L, -1 );
    switch( err ) {
        case LUA_ERRSYNTAX:
            if( !simple ) {
                lua_error_stream << "Lua returned syntax error for "  << path  << std::endl;
            }
            lua_error_stream << error;
            break;
        case LUA_ERRMEM:
            lua_error_stream << "Lua is out of memory";
            break;
        case LUA_ERRFILE:
            if( !simple ) {
                lua_error_stream << "Lua returned file io error for " << path << std::endl;
            }
            lua_error_stream << error;
            break;
        default:
            if( !simple ) {
                lua_error_stream << string_format( "Lua returned unknown error %d for ", err ) << path << std::endl;
            }
            lua_error_stream << error;
            break;
    }
    return true;
}

/**
 * This is the basic type-checking interface for the Lua bindings generator.
 * Instead of "if type is string, call lua_isstring, if it's int, call lua_isnumber, ...", the
 * generator can just call "LuaType<"..type..">::has".
 * The C++ classes do the actual separation based on the type through the template parameter.
 *
 * Each implementation contains function like the LuaValue has:
 * - @ref has checks whether the object at given stack index is of the requested type.
 * - @ref check calls @ref has and issues a Lua error if the types is not as requested.
 * - @ref get returns the value at given stack_index. This is like @ref LuaValue::get.
 *   If you need to store the value, use \code auto && val = LuaType<X>::get(...); \endcode
 * - @ref push puts the value on the stack, like @ref LuaValue::push
 */
template<typename T>
struct LuaType;

template<>
struct LuaType<int> { // typing functions for cata's lua?
    static bool has( lua_State *const L, const int stack_index ) { // check if (lua stack index) is a number.
        return lua_isnumber( L, stack_index );
    }
    static void check( lua_State *const L, const int stack_index ) { // check type
        luaL_checktype( L, stack_index, LUA_TNUMBER );
    }
    static int get( lua_State *const L, const int stack_index ) { // turn (lua stack index) into a number
        return lua_tonumber( L, stack_index );
    }
    static void push( lua_State *const L, const int value ) { // push number to lua stack
        lua_pushnumber( L, value );
    }
};
template<>
struct LuaType<bool> { // typing function checks (true/false) for cata's lua?
    static bool has( lua_State *const L, const int stack_index ) {
        return lua_isboolean( L, stack_index );
    }
    static void check( lua_State *const L, const int stack_index ) {
        luaL_checktype( L, stack_index, LUA_TBOOLEAN );
    }
    static bool get( lua_State *const L, const int stack_index ) {
        return lua_toboolean( L, stack_index );
    }
    static void push( lua_State *const L, const bool value ) {
        lua_pushboolean( L, value );
    }
    // It is helpful to be able to treat optionals as bools when passing to lua
    template<typename T>
    static void push( lua_State *const L, const cata::optional<T> &value ) {
        push( L, !!value );
    }
};
template<> // typing functions for cata's Lua (pertaining to strings)?
struct LuaType<std::string> {
    static bool has( lua_State *const L, const int stack_index ) {
        return lua_isstring( L, stack_index );
    }
    static void check( lua_State *const L, const int stack_index ) {
        luaL_checktype( L, stack_index, LUA_TSTRING );
    }
    static std::string get( lua_State *const L, const int stack_index ) {
        return lua_tostring_wrapper( L, stack_index );
    }
    static void push( lua_State *const L, const std::string &value ) {
        lua_pushlstring( L, value.c_str(), value.length() );
    }
    // For better performance: if the input is a c-string, forward it as such without wrapping
    // it into a std::string first.
    static void push( lua_State *const L, const char *value ) {
        lua_pushstring( L, value );
    }
};
template<> // typing functions for cata's Lua (pertaining to float)?
struct LuaType<float> : public LuaType<int> { // inherit checking because it's all the same to Lua
    static float get( lua_State *const L, const int stack_index ) {
        return lua_tonumber( L, stack_index );
    }
    static void push( lua_State *const L, const float value ) {
        lua_pushnumber( L, value );
    }
};
template<typename T> // typing functions but for lua values for cata's lua...?
struct LuaType<LuaValue<T>> : public LuaValue<T> {
};
template<typename T> // typing functions but for LuaReferences for cata's lua...?
struct LuaType<LuaReference<T>> : public LuaReference<T> {
};

/** This basically transforms a string (therefore inheriting from LuaType<string>) into a C++
 * enumeration value. It simply contains a table of string-to-enum-values. */
template<typename E>
class LuaEnum : private LuaType<std::string>
{
    private:
        using Parent = LuaType<std::string>;
        /** Defined by generate_bindings.lua in catabindings.cpp */
        using EMap = std::map<std::string, E>;
        static const EMap BINDINGS;
        static E from_string( const std::string &value ) {
            const auto iter = BINDINGS.find( value );
            if( iter == BINDINGS.end() ) {
                // This point shall not be reached. Always call this with valid input.
                return BINDINGS.begin()->second;
            }
            return iter->second;
        }
        static const std::string &to_string( E const value ) {
            for( auto &e : BINDINGS ) {
                if( e.second == value ) {
                    return e.first;
                }
            }
            // This point shall not be reached. Always call this with valid input.
            return BINDINGS.begin()->first;
        }
        static bool has( const std::string &value ) {
            return BINDINGS.count( value ) > 0;
        }
        static int index( lua_State *const L ) {
            // -1 is the key (function to call)
            const char *const key = lua_tostring( L, -1 );
            if( key == nullptr ) {
                luaL_error( L, "Invalid input to __index: key is not a string." );
            }
            const auto iter = BINDINGS.find( key );
            if( iter == BINDINGS.end() ) {
                return luaL_error( L, "Invalid enum value." );
            }
            lua_remove( L, -1 ); // remove key
            // Push the enum as string, it will be converted back to the enum later. This way, it can
            // be specified both ways in Lua code: either as string or via an entry here.
            lua_pushlstring( L, iter->first.c_str(), iter->first.length() );
            return 1;
        }
    public:
        static bool has( lua_State *const L, const int stack_index ) {
            return Parent::has( L, stack_index ) && has( Parent::get( L, stack_index ) );
        }
        static void check( lua_State *const L, const int stack_index ) {
            Parent::check( L, stack_index );
            if( !has( Parent::get( L, stack_index ) ) ) {
                luaL_argerror( L, stack_index, "invalid value for enum" );
            }
        }
        static E get( lua_State *const L, const int stack_index ) {
            return from_string( Parent::get( L, stack_index ) );
        }
        static void push( lua_State *const L, E const value ) {
            Parent::push( L, to_string( value ) );
        }
        /** Export the enum values as entries of a global metatable */
        static void export_global( lua_State *const L, const char *global_name ) {
            lua_createtable( L, 0, 1 ); // +1
            lua_pushvalue( L, -1 ); // + 1
            // Set the new table to have itself as metatable
            lua_setmetatable( L, -2 ); // -1
            // Setup the __index entry, which will translate the entry to a enum value
            lua_pushcfunction( L, &index ); // +1
            lua_setfield( L, -2, "__index" ); // -1
            // And register as a global value
            lua_setglobal( L, global_name ); // -1
        }
};
template<typename E> // lua (class) for Lua enum types...?
struct LuaType<LuaEnum<E>> : public LuaEnum<E> {
};

/**
 * Wrapper class to access objects in Lua that are stored as either a pointer or a value.
 * Technically, this class could inherit from both `LuaValue<T>` and `LuaReference<T>`,
 * but that would basically the same code anyway.
 * It behaves like a LuaValue if there is a value on the stack, and like LuaReference is there
 * is a reference on the stack. Functions behave like the functions in a `LuaType`.
 * Note that it does not have a push function because it can not know whether to push a reference
 * or a value (copy). The caller must decide this and must use `LuaValue` or `LuaReference`.
 */
template<typename T>
class LuaValueOrReference
{
    public:
        using proxy = typename LuaReference<T>::proxy;
        static proxy get( lua_State *const L, const int stack_index ) {
            if( LuaValue<T>::has( L, stack_index ) ) {
                return proxy{ &LuaValue<T>::get( L, stack_index ) };
            }
            return LuaReference<T>::get( L, stack_index );
        }
        static void check( lua_State *const L, const int stack_index ) {
            if( LuaValue<T>::has( L, stack_index ) ) {
                return;
            }
            LuaValue<T *>::check( L, stack_index );
        }
        static bool has( lua_State *const L, const int stack_index ) {
            return LuaValue<T>::has( L, stack_index ) || LuaValue<T *>::has( L, stack_index );
        }
};

void update_globals( lua_State *L ) // update all global data?
{
    //LuaReference<player>::push( L, g->u ); // get_player_character
    //LuaReference<player>::push( L, get_player_character() ); // modernization: use modern method of retrieving the player character
    LuaReference<avatar>::push( L, get_avatar() ); // modernization: use modern method of retrieving the player character('s avatar)
    luah_setglobal( L, "player", -1 );
    // luah_setglobal pushes an extra copy of the global data before storing it,
    // but here the original value isn't needed once the global data has been
    // saved.
    lua_pop( L, 1 );

    LuaReference<map>::push( L, get_map() ); // modernization: use get_map() instead of accessing private var directly
    luah_setglobal( L, "map", -1 );
    lua_pop( L, 1 );

    LuaReference<game>::push( L, g.get() );
    luah_setglobal( L, "g", -1 );
    lua_pop( L, 1 );
}

// "lua iuse action stuff" was here

// iuse abstraction to make iuse's both in lua and C++ possible
// ------------------------------------------------------------
void Item_factory::register_iuse_lua( const std::string &name, int lua_function )
{
    if( iuse_function_list.count( name ) > 0 ) {
        DebugLog( D_INFO, D_MAIN ) << "lua iuse function " << name << " overrides existing iuse function";
    }
    iuse_function_list[name] = use_function( new lua_iuse_wrapper( lua_function, name ) );
}

class lua_mattack_wrapper : public mattack_actor
{
    private:
        int lua_function;

    public:
        lua_mattack_wrapper( const mattack_id &id, const int f ) :
            mattack_actor( id ),
            lua_function( f ) {}

        ~lua_mattack_wrapper() override = default;

        bool call( monster &m ) const override {
            lua_State *const L = lua_state;
            // If it's a lua function, the arguments have to be wrapped in
            // lua userdata's and passed on the lua stack.
            // We will now call the function f(monster)
            update_globals( L );
            // Push the lua function on top of the stack
            lua_rawgeti( L, LUA_REGISTRYINDEX, lua_function );
            // Push the monster on top of the stack.
            const int monster_in_registry = LuaReference<monster>::push_reg( L, m );
            // Call the iuse function
            int err = lua_pcall( L, 1, 1, 0 );
            lua_report_error( L, err, "monattack function" );
            // Make sure the now outdated parameters we passed to lua aren't
            // being used anymore by setting a metatable that will error on
            // access.
            luah_remove_from_registry( L, monster_in_registry );
            luah_setmetatable( L, "outdated_metatable" );
            return lua_toboolean( L, -1 );
        }

        mattack_actor *clone() const override {
            return new lua_mattack_wrapper( *this );
        }

        void load_internal( JsonObject &, const std::string & ) override {}
};

void MonsterGenerator::register_monattack_lua( const std::string &name, int lua_function )
{
    add_attack( mtype_special_attack( new lua_mattack_wrapper( name, lua_function ) ) );
}

// Call the given string directly, used in the lua debug command.
int call_lua( const std::string &tocall )
{
    lua_State *L = lua_state;

    update_globals( L );
    int err = luaL_dostring( L, tocall.c_str() );
    lua_report_error( L, err, tocall.c_str(), true );
    return err;
}
/*  */
void CallbackArgument::Save()
{
    lua_State *const L = lua_state; // lua state
    switch( type ) { // switch between cases
        case CallbackArgumentType::Integer: // if this arg is an INTEGER
            lua_pushinteger( L, value_integer );
            break;
        case CallbackArgumentType::Number: // if this arg is a NUMBER
            lua_pushnumber( L, value_number );
            break;
        case CallbackArgumentType::Boolean: // if this arg is a BOOLEAN
            lua_pushboolean( L, value_boolean );
            break;
        case CallbackArgumentType::String: // if this arg is a STRING
            lua_pushstring( L, value_string.c_str() );
            break;
        case CallbackArgumentType::Tripoint: // if this arg is a TRIPOINT (coords)
            LuaValue<tripoint>::push( L, value_tripoint );
            break;
        case CallbackArgumentType::Item: // if this arg is an ITEM
            LuaValue<item>::push( L, value_item );
            break;
        case CallbackArgumentType::Reference_Creature: // if this arg is a REFERENCE CREATURE
            LuaReference<Creature>::push( L, value_creature );
            break;
        case CallbackArgumentType::Enum_BodyPart: // if this arg is an ENUM_BODYPART
            LuaEnum<body_part>::push( L, value_body_part );
            break;
        case CallbackArgumentType::Id_BodyPart:
            LuaValue<const int_id<body_part_type>>::push( L, value_body_part_id ); // CAT_BDP
            break;
        case CallbackArgumentType::Character_Id:
            LuaValue<character_id>::push( L, value_character_id ); // CAT_CHARACTER_ID
            break;
        case CallbackArgumentType::Weather_Id:
            LuaValue<weather_type_id>::push( L, value_weather_id ); // CAT_WEATHER_ID
            break;
        default:
            lua_pushnil( L );
            break;
    }
}
// Handles lua callbacks; I think this is the thing that 
void lua_callback_helper( const char *callback_name, const CallbackArgumentContainer &callback_args,
                          int retsize = 0 )
{
    if( lua_state == nullptr ) {
        return;
    }
    lua_State *L = lua_state;
    update_globals( L );
    lua_getglobal( L, "mod_callback" );
    lua_pushstring( L, callback_name );
    for( auto callback_arg : callback_args ) { // for each argument
        callback_arg.Save(); // save
    }
    int err = lua_pcall( L, callback_args.size() + 1, retsize, 0 ); // executes a lua function using the lua_state. iirc, lua pcalls are lua try/catches; they handle errors, as they are in "protected" mode (hence the `p` in `pcall`)
    std::string err_function = "mod_callback(\"" + std::string( callback_name ) + "\")";
    lua_report_error( L, err, err_function.c_str(), true );
}

void lua_callback( const char *callback_name, const CallbackArgumentContainer &callback_args ) // does the lua callback stuff
{
    lua_callback_helper( callback_name, callback_args );
}

void lua_callback( const char *callback_name )
{
    CallbackArgumentContainer callback_args;
    lua_callback( callback_name, callback_args );
}

std::string lua_callback_getstring( const char *callback_name,
                                    const CallbackArgumentContainer &callback_args )
{
    lua_callback_helper( callback_name, callback_args, 1 );
    lua_State *L = lua_state;
    size_t len;
    const char *tmp = lua_tolstring( L, -1, &len );
    std::string retval = tmp ? tmp : "";
    return retval;
}

// Generate a lua-based map.
// int lua_mapgen( map *m, const oter_id &terrain_type, const mapgendata &, const time_point &t, float,
//                 const std::string &scr ) // oldcode
int lua_mapgen( map *m, const oter_id &terrain_type, const std::string &scr )
{
    if( lua_state == nullptr ) { // if the lua state is a null pointer...
        return 0; // return 0.
    }
    lua_State *L = lua_state; // Set funky lua state to the non-null lua state var (address) ?
    LuaReference<map>::push( L, m ); // push <luastate, map>
    luah_setglobal( L, "map", -1 ); // set global (var?) "map" to -1 (i think?) using the funky lua state pointer

    int err = luaL_loadstring( L, scr.c_str() ); // error var
    if( lua_report_error( L, err, scr.c_str() ) ) { // if there's a lua error, return the error & halt lua mapgen execution
        return err; // return errorcode
    }
    //    int function_index = luaL_ref(L, LUA_REGISTRYINDEX); // @todo; make use of this
    //    lua_rawgeti(L, LUA_REGISTRYINDEX, function_index);

    lua_pushstring( L, terrain_type.id().c_str() ); // push the terrain type's id to the stack
    lua_setglobal( L, "tertype" ); // push "tertype" to stack (global?)
    lua_pushinteger( L, to_turn<int>( t ) ); // push `int`egerized to_turn variable (t, AKA timepoint) to stack
    lua_setglobal( L, "turn" ); // push "turn" to stack

    err = lua_pcall( L, 0, LUA_MULTRET, 0 ); // protected call; set var to the following given errors
    lua_report_error( L, err, scr.c_str() ); // report all errors to `err`

    //    luah_remove_from_registry(L, function_index); // @todo: make use of this

    return err; // return error data (which would pretty much be an integer)
}

// Custom functions that are to be wrapped from lua.
// -------------------------------------------------
static std::unique_ptr<uilist> uilist_instance;
uilist *create_uilist()
{
    uilist_instance.reset( new uilist() );
    return uilist_instance.get();
}

// Simulate old create_uimenu() behavior
uilist *create_uilist_no_cancel()
{
    uilist_instance.reset( new uilist() );
    uilist_instance->allow_cancel = false;
    return uilist_instance.get();
}

const ter_t &get_terrain_type( int id )
{
    return ter_id( id ).obj();
}

static calendar &get_calendar_turn_wrapper()
{
    return calendar::turn;
}

static time_duration get_time_duration_wrapper( const int t )
{
    return time_duration::from_turns( t );
}

static std::string get_omt_id( const overmap &om, const tripoint &p )
{
    return om.get_ter( p ).id().str();
}

static overmap_direction get_omt_dir( const overmap &om, const tripoint &p )
{
    return om.get_ter( p ).obj().get_dir();
}

static std::string string_input_popup_wrapper( const std::string &title, int width,
        const std::string &desc )
{
    return string_input_popup().title( title ).width( width ).description( desc ).query_string();
}

/** Get reference to monster at given tripoint. */
monster *get_monster_at( const tripoint &p )
{
    return g->critter_at<monster>( p );
}

/** Get reference to Creature at given tripoint. */
Creature *get_critter_at( const tripoint &p )
{
    return g->critter_at( p );
}

/** Create a new monster of the given type. */
monster *create_monster( const mtype_id &mon_type, const tripoint &p )
{
    monster new_monster( mon_type, p );
    if( !g->add_zombie( new_monster ) ) {
        return nullptr;
    } else {
        return g->critter_at<monster>( p );
    }
}

// Manually implemented lua functions
//
// Most lua functions are generated by src/lua/generate_bindings.lua,
// these generated functions can be found in src/lua/catabindings.cpp

static void popup_wrapper( const std::string &text )
{
    popup( "%s", text.c_str() ); // call Cataclysm's `popup` function
}
// wrapper for Cataclysm: Dark Days Ahead's `add_msg` (adds message to announcements pane)
static void add_msg_wrapper( const std::string &text )
{
    add_msg( text ); // call Cataclysm's `add_msg()` function
}
// wrapper for Cataclysm's Y/N query pop up.
static bool query_yn_wrapper( const std::string &text )
{
    return query_yn( text ); // call (and return the result of) Cataclysm's `query_yn()` function
}

// items = game.items_at(x, y)
static int game_items_at( lua_State *L )
{
    int x = lua_tointeger( L, 1 ); // turns an int to a Lua integer?
    int y = lua_tointeger( L, 2 ); // turns an int to a Lua integer?

    auto items = get_map().i_at( x, y ); // modernization: use getter instead of directly accessing private variable.
    lua_createtable( L, items.size(), 0 ); // Preallocate enough space for all our items.

    // Iterate over the monster list and insert each monster into our returned table.
    int i = 0;
    for( auto &an_item : items ) {
        // The stack will look like this:
        // 1 - t, table containing item
        // 2 - k, index at which the next item will be inserted
        // 3 - v, next item to insert
        //
        // lua_rawset then does t[k] = v and pops v and k from the stack

        lua_pushnumber( L, i++ + 1 );
        item **item_userdata = ( item ** ) lua_newuserdata( L, sizeof( item * ) );
        *item_userdata = &an_item;
        // TODO: update using LuaReference<item>
        luah_setmetatable( L, "item_metatable" );
        lua_rawset( L, -3 );
    }

    return 1; // 1 return values
}

// item_groups = game.get_item_groups()
static int game_get_item_groups( lua_State *L )
{
    std::vector<std::string> items = item_controller->get_all_group_names();

    lua_createtable( L, items.size(), 0 ); // Preallocate enough space for all our items.

    // Iterate over the monster list and insert each monster into our returned table.
    for( size_t i = 0; i < items.size(); ++i ) {
        // The stack will look like this:
        // 1 - t, table containing item
        // 2 - k, index at which the next item will be inserted
        // 3 - v, next item to insert
        //
        // lua_rawset then does t[k] = v and pops v and k from the stack

        lua_pushnumber( L, i + 1 );
        lua_pushstring( L, items[i].c_str() );
        lua_rawset( L, -3 );
    }

    return 1; // 1 return values
}

// monster_types = game.get_monster_types()
static int game_get_monster_types( lua_State *L )
{
    const auto mtypes = MonsterGenerator::generator().get_all_mtypes();

    lua_createtable( L, mtypes.size(), 0 ); // Preallocate enough space for all our monster types.

    // Iterate over the monster list and insert each monster into our returned table.
    for( size_t i = 0; i < mtypes.size(); ++i ) {
        // The stack will look like this:
        // 1 - t, table containing id
        // 2 - k, index at which the next id will be inserted
        // 3 - v, next id to insert
        //
        // lua_rawset then does t[k] = v and pops v and k from the stack

        lua_pushnumber( L, i + 1 );
        LuaValue<mtype_id>::push( L, mtypes[i].id );
        lua_rawset( L, -3 );
    }

    return 1; // 1 return values
}

// x, y = choose_adjacent(query_string, x, y)
static int game_choose_adjacent( lua_State *L )
{
    const std::string parameter1 = lua_tostring_wrapper( L, 1 );
    const cata::optional<tripoint> pnt = choose_adjacent( parameter1 );
    if( pnt ) {
        lua_pushnumber( L, pnt->x );
        lua_pushnumber( L, pnt->y );
        lua_pushnumber( L, pnt->z );
        return 3; // 3 return values
    } else {
        return 0; // 0 return values
    }
}

// game.register_iuse(string, function_object)
static int game_register_iuse( lua_State *L )
{
    // Make sure the first argument is a string.
    const char *name = luaL_checkstring( L, 1 );
    if( !name ) {
        return luaL_error( L, "First argument to game.register_iuse is not a string." );
    }

    // Make sure the second argument is a function
    luaL_checktype( L, 2, LUA_TFUNCTION );

    // function_object is at the top of the stack, so we can just pop
    // it with luaL_ref
    int function_index = luaL_ref( L, LUA_REGISTRYINDEX );

    // Now register function_object with our iuse's
    item_controller->register_iuse_lua( name, function_index );

    return 0; // 0 return values
}

static int game_register_monattack( lua_State *L )
{
    // Make sure the first argument is a string.
    const char *name = luaL_checkstring( L, 1 );
    if( !name ) {
        return luaL_error( L, "First argument to game.register_monattack is not a string." );
    }
    // Make sure the second argument is a function
    luaL_checktype( L, 2, LUA_TFUNCTION );
    // function_object is at the top of the stack, so we can just pop
    // it with luaL_ref
    int function_index = luaL_ref( L, LUA_REGISTRYINDEX );
    // Now register function_object with our monattack's
    MonsterGenerator::generator().register_monattack_lua( name, function_index );
    return 0; // 0 return values
}

#include "lua/catabindings.cpp"

// Load the main file of a mod
void lua_loadmod( const std::string &base_path, const std::string &main_file_name )
{
    std::string full_path = base_path + "/" + main_file_name;
    if( file_exist( full_path ) ) {
        lua_file_path = base_path;
        lua_dofile( lua_state, full_path.c_str() );
        lua_file_path.clear();
    }
    // debugmsg("Loading from %s", full_path.c_str());
}

// Custom error handler
static int traceback( lua_State *L )
{
    // Get the error message
    const std::string error = lua_tostring_wrapper( L, -1 );

    // Get the lua stack trace
#if LUA_VERSION_NUM < 502
    lua_getfield( L, LUA_GLOBALSINDEX, "debug" );
    lua_getfield( L, -1, "traceback" );
#else
    lua_getglobal( L, "debug" );
    lua_getfield( L, -1, "traceback" );
    lua_remove( L, -2 );
#endif
    lua_pushvalue( L, 1 );
    lua_pushinteger( L, 2 );
    lua_call( L, 2, 1 );

    const std::string stacktrace = lua_tostring_wrapper( L, -1 );

    // Print a debug message.
    debugmsg( "Error in lua module: %s", error.c_str() );

    // Print the stack trace to our debug log.
    DebugLog( D_ERROR, DC_ALL ) << stacktrace;
    return 1;
}

// Load an arbitrary lua file
void lua_dofile( lua_State *L, const char *path )
{
    lua_pushcfunction( L, &traceback );
    int err = luaL_loadfile( L, path );
    if( lua_report_error( L, err, path ) ) {
        return;
    }
    err = lua_pcall( L, 0, LUA_MULTRET, -2 );
    lua_report_error( L, err, path );
}

// game.dofile(file)
//
// Method to load files from lua, later should be made "safe" by
// ensuring it's being loaded from a valid path etc.
static int game_dofile( lua_State *L )
{
    const char *path = luaL_checkstring( L, 1 );

    std::string full_path = lua_file_path + "/" + path;
    lua_dofile( L, full_path.c_str() );
    return 0;
}

static int game_myPrint( lua_State *L )
{
    int argc = lua_gettop( L );
    for( int i = argc; i > 0; i-- ) {
        lua_output_stream << lua_tostring_wrapper( L, -i );
    }
    lua_output_stream << std::endl;
    return 0;
}

// Registry containing all the game functions exported to lua.
// -----------------------------------------------------------
static const struct luaL_Reg global_funcs [] = {
    {"register_iuse", game_register_iuse},
    {"register_monattack", game_register_monattack},
    //{"get_monsters", game_get_monsters},
    {"items_at", game_items_at},
    {"choose_adjacent", game_choose_adjacent},
    {"dofile", game_dofile},
    {"get_monster_types", game_get_monster_types},
    {"get_item_groups", game_get_item_groups},
    {nullptr, nullptr}
};

// Lua initialization.
void game::init_lua()
{
    // This is called on each new-game, the old state (if any) is closed to dispose any data
    // introduced by mods of the previously loaded world.
    if( lua_state != nullptr ) {
        lua_close( lua_state );
    }
    lua_state = luaL_newstate();
    if( lua_state == nullptr ) {
        debugmsg( "Failed to start Lua. Lua scripting won't be available." );
        return;
    }

    luaL_openlibs( lua_state ); // Load standard lua libs

    // Load our custom "game" module
#if LUA_VERSION_NUM < 502
    luaL_register( lua_state, "game", gamelib );
    luaL_register( lua_state, "game", global_funcs );
#else
    std::vector<luaL_Reg> lib_funcs;
    for( auto x = gamelib; x->name != nullptr; ++x ) {
        lib_funcs.push_back( *x );
    }
    for( auto x = global_funcs; x->name != nullptr; ++x ) {
        lib_funcs.push_back( *x );
    }
    lib_funcs.push_back( luaL_Reg { nullptr, nullptr } );
    luaL_newmetatable( lua_state, "game" );
    lua_pushvalue( lua_state, -1 );
    luaL_setfuncs( lua_state, &lib_funcs.front(), 0 );
    lua_setglobal( lua_state, "game" );
#endif

    load_metatables( lua_state );
    LuaEnum<body_part>::export_global( lua_state, "body_part" );

    // override default print to our version
    lua_register( lua_state, "print", game_myPrint );

    // Load lua-side metatables etc.
    lua_dofile( lua_state, FILENAMES["class_defslua"].c_str() );
    lua_dofile( lua_state, FILENAMES["autoexeclua"].c_str() );
}

#endif // #ifdef LUA; ends the "if lua was defined" preprocessor definitions
// iuse.h code is getting redefined here for some reason? I don't think this belongs here...
// use_function::use_function( const use_function &other )
//     : actor( other.actor ? other.actor->clone() : nullptr )
// {
// }

// use_function &use_function::operator=( iuse_actor *const f )
// {
//     return operator=( use_function( f ) );
// }

// use_function &use_function::operator=( const use_function &other )
// {
//     actor.reset( other.actor ? other.actor->clone() : nullptr );
//     return *this;
// }

// void use_function::dump_info( const item &it, std::vector<iteminfo> &dump ) const
// {
//     if( actor != nullptr ) {
//         actor->info( it, dump );
//     }
// }

// ret_val<bool> use_function::can_call( const player &p, const item &it, bool t,
//                                       const tripoint &pos ) const
// {
//     if( actor == nullptr ) {
//         return ret_val<bool>::make_failure( _( "You can't do anything interesting with your %s." ),
//                                             it.tname().c_str() );
//     }

//     return actor->can_use( p, it, t, pos );
// }

// long use_function::call( player &p, item &it, bool active, const tripoint &pos ) const
// {
//     return actor->use( p, it, active, pos );
// }
// "iuse"s were redefined here for some reason. I think it'd be best to commend this entire "iuse" code out 
#ifndef LUA // if LUA is not defined...
/* Empty functions for builds without Lua: */
int lua_monster_move( monster * )
{
    return 0;
}
//int call_lua( std::string )
int call_lua( const std::string & )
{
    popup( _( "This binary was not compiled with Lua support." ) );
    return 0;
}
// Implemented in mapgen.cpp:
// int lua_mapgen( map *, std::string, mapgendata, int, float, const std::string & )
void lua_loadmod( const std::string &, const std::string & ) // loads lua mods?
{
}
void game::init_lua() // game initializes lua?
{
}

void lua_callback( const char *, const CallbackArgumentContainer & ) // lua callbacks using a container?
{
}
void lua_callback( const char * ) // general lua callback?
{
}

#endif // since lua was not defined, make all of these functions empty.