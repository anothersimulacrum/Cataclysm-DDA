#include "mapdata.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

#include "assign.h"
#include "calendar.h"
#include "color.h"
#include "debug.h"
#include "enum_conversions.h"
#include "generic_factory.h"
#include "harvest.h"
#include "iexamine.h"
#include "iexamine_actors.h"
#include "item_group.h"
#include "json.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"
#include "trap.h"

static const std::string flag_DIGGABLE( "DIGGABLE" );
static const std::string flag_LOCKED( "LOCKED" );
static const std::string flag_TRANSPARENT( "TRANSPARENT" );

namespace
{

const units::volume DEFAULT_MAX_VOLUME_IN_SQUARE = units::from_liter( 1000 );

generic_factory<ter_t> terrain_data( "terrain" );
generic_factory<furn_t> furniture_data( "furniture" );

} // namespace

/** @relates int_id */
template<>
inline bool int_id<ter_t>::is_valid() const
{
    return terrain_data.is_valid( *this );
}

/** @relates int_id */
template<>
const ter_t &int_id<ter_t>::obj() const
{
    return terrain_data.obj( *this );
}

/** @relates int_id */
template<>
const string_id<ter_t> &int_id<ter_t>::id() const
{
    return terrain_data.convert( *this );
}

/** @relates int_id */
template<>
int_id<ter_t> string_id<ter_t>::id() const
{
    return terrain_data.convert( *this, t_null );
}

/** @relates int_id */
template<>
int_id<ter_t>::int_id( const string_id<ter_t> &id ) : _id( id.id() )
{
}

/** @relates string_id */
template<>
const ter_t &string_id<ter_t>::obj() const
{
    return terrain_data.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<ter_t>::is_valid() const
{
    return terrain_data.is_valid( *this );
}

/** @relates int_id */
template<>
bool int_id<furn_t>::is_valid() const
{
    return furniture_data.is_valid( *this );
}

/** @relates int_id */
template<>
const furn_t &int_id<furn_t>::obj() const
{
    return furniture_data.obj( *this );
}

/** @relates int_id */
template<>
const string_id<furn_t> &int_id<furn_t>::id() const
{
    return furniture_data.convert( *this );
}

/** @relates string_id */
template<>
bool string_id<furn_t>::is_valid() const
{
    return furniture_data.is_valid( *this );
}

/** @relates string_id */
template<>
const furn_t &string_id<furn_t>::obj() const
{
    return furniture_data.obj( *this );
}

/** @relates string_id */
template<>
int_id<furn_t> string_id<furn_t>::id() const
{
    return furniture_data.convert( *this, f_null );
}

/** @relates int_id */
template<>
int_id<furn_t>::int_id( const string_id<furn_t> &id ) : _id( id.id() )
{
}

static const std::unordered_map<std::string, ter_bitflags> ter_bitflags_map = { {
        { "DESTROY_ITEM",             TFLAG_DESTROY_ITEM },   // add/spawn_item*()
        { "ROUGH",                    TFLAG_ROUGH },          // monmove
        { "UNSTABLE",                 TFLAG_UNSTABLE },       // monmove
        { "LIQUID",                   TFLAG_LIQUID },         // *move(), add/spawn_item*()
        { "FIRE_CONTAINER",           TFLAG_FIRE_CONTAINER }, // fire
        { "DIGGABLE",                 TFLAG_DIGGABLE },       // monmove
        { "SUPPRESS_SMOKE",           TFLAG_SUPPRESS_SMOKE }, // fire
        { "FLAMMABLE_HARD",           TFLAG_FLAMMABLE_HARD }, // fire
        { "SEALED",                   TFLAG_SEALED },         // Fire, acid
        { "ALLOW_FIELD_EFFECT",       TFLAG_ALLOW_FIELD_EFFECT }, // Fire, acid
        { "COLLAPSES",                TFLAG_COLLAPSES },      // building "remodeling"
        { "FLAMMABLE",                TFLAG_FLAMMABLE },      // fire bad! fire SLOW!
        { "REDUCE_SCENT",             TFLAG_REDUCE_SCENT },   // ...and the other half is update_scent
        { "INDOORS",                  TFLAG_INDOORS },        // vehicle gain_moves, weather
        { "SHARP",                    TFLAG_SHARP },          // monmove
        { "SUPPORTS_ROOF",            TFLAG_SUPPORTS_ROOF },  // and by building "remodeling" I mean hulkSMASH
        { "MINEABLE",                 TFLAG_MINEABLE },       // allows mining
        { "SWIMMABLE",                TFLAG_SWIMMABLE },      // monmove, many fields
        { "TRANSPARENT",              TFLAG_TRANSPARENT },    // map::is_transparent / lightmap
        { "NOITEM",                   TFLAG_NOITEM },         // add/spawn_item*()
        { "NO_SIGHT",                 TFLAG_NO_SIGHT },       // Sight reduced to 1 on this tile
        { "FLAMMABLE_ASH",            TFLAG_FLAMMABLE_ASH },  // oh hey fire. again.
        { "WALL",                     TFLAG_WALL },           // connects to other walls
        { "NO_SHOOT",                 TFLAG_NO_SHOOT },       // terrain cannot be damaged by ranged attacks
        { "NO_SCENT",                 TFLAG_NO_SCENT },       // cannot have scent values, which prevents scent diffusion through this tile
        { "DEEP_WATER",               TFLAG_DEEP_WATER },     // Deep enough to submerge things
        { "SHALLOW_WATER",            TFLAG_SHALLOW_WATER },  // Water, but not deep enough to submerge the player
        { "CURRENT",                  TFLAG_CURRENT },        // Water is flowing.
        { "HARVESTED",                TFLAG_HARVESTED },      // harvested.  will not bear fruit.
        { "PERMEABLE",                TFLAG_PERMEABLE },      // gases can flow through.
        { "AUTO_WALL_SYMBOL",         TFLAG_AUTO_WALL_SYMBOL }, // automatically create the appropriate wall
        { "CONNECT_TO_WALL",          TFLAG_CONNECT_TO_WALL }, // superseded by ter_connects, retained for json backward compatibility
        { "CLIMBABLE",                TFLAG_CLIMBABLE },      // Can be climbed over
        { "GOES_DOWN",                TFLAG_GOES_DOWN },      // Allows non-flying creatures to move downwards
        { "GOES_UP",                  TFLAG_GOES_UP },        // Allows non-flying creatures to move upwards
        { "NO_FLOOR",                 TFLAG_NO_FLOOR },       // Things should fall when placed on this tile
        { "SEEN_FROM_ABOVE",          TFLAG_SEEN_FROM_ABOVE },// This should be visible if the tile above has no floor
        { "HIDE_PLACE",               TFLAG_HIDE_PLACE },     // Creature on this tile can't be seen by other creature not standing on adjacent tiles
        { "BLOCK_WIND",               TFLAG_BLOCK_WIND },     // This tile will partially block the wind.
        { "FLAT",                     TFLAG_FLAT },           // This tile is flat.
        { "RAMP",                     TFLAG_RAMP },           // Can be used to move up a z-level
        { "RAMP_DOWN",                TFLAG_RAMP_DOWN },      // Anything entering this tile moves down a z-level
        { "RAMP_UP",                  TFLAG_RAMP_UP },        // Anything entering this tile moves up a z-level
        { "RAIL",                     TFLAG_RAIL },           // Rail tile (used heavily)
        { "THIN_OBSTACLE",            TFLAG_THIN_OBSTACLE },  // Passable by players and monsters. Vehicles destroy it.
        { "Z_TRANSPARENT",            TFLAG_Z_TRANSPARENT },  // Doesn't block vision passing through the z-level
        { "SMALL_PASSAGE",            TFLAG_SMALL_PASSAGE },   // A small passage, that large or huge things cannot pass through
        { "SUN_ROOF_ABOVE",           TFLAG_SUN_ROOF_ABOVE },   // This furniture has a "fake roof" above, that blocks sunlight (see #44421).
        { "FUNGUS",                   TFLAG_FUNGUS }            // Fungal covered.
    }
};

static const std::unordered_map<std::string, ter_connects> ter_connects_map = { {
        { "WALL",                     TERCONN_WALL },         // implied by TFLAG_CONNECT_TO_WALL, TFLAG_AUTO_WALL_SYMBOL or TFLAG_WALL
        { "CHAINFENCE",               TERCONN_CHAINFENCE },
        { "WOODFENCE",                TERCONN_WOODFENCE },
        { "RAILING",                  TERCONN_RAILING },
        { "WATER",                    TERCONN_WATER },
        { "POOLWATER",                TERCONN_POOLWATER },
        { "PAVEMENT",                 TERCONN_PAVEMENT },
        { "RAIL",                     TERCONN_RAIL },
        { "COUNTER",                  TERCONN_COUNTER },
        { "CANVAS_WALL",              TERCONN_CANVAS_WALL },
    }
};

static void load_map_bash_tent_centers( const JsonArray &ja, std::vector<furn_str_id> &centers )
{
    for( const std::string line : ja ) {
        centers.emplace_back( line );
    }
}

map_bash_info::map_bash_info() : str_min( -1 ), str_max( -1 ),
    str_min_blocked( -1 ), str_max_blocked( -1 ),
    str_min_supported( -1 ), str_max_supported( -1 ),
    explosive( 0 ), sound_vol( -1 ), sound_fail_vol( -1 ),
    collapse_radius( 1 ), destroy_only( false ), bash_below( false ),
    drop_group( "EMPTY_GROUP" ),
    ter_set( ter_str_id::NULL_ID() ), furn_set( furn_str_id::NULL_ID() ) {}

bool map_bash_info::load( const JsonObject &jsobj, const std::string &member,
                          map_object_type obj_type, const std::string &context )
{
    if( !jsobj.has_object( member ) ) {
        return false;
    }

    JsonObject j = jsobj.get_object( member );
    str_min = j.get_int( "str_min", 0 );
    str_max = j.get_int( "str_max", 0 );

    str_min_blocked = j.get_int( "str_min_blocked", -1 );
    str_max_blocked = j.get_int( "str_max_blocked", -1 );

    str_min_supported = j.get_int( "str_min_supported", -1 );
    str_max_supported = j.get_int( "str_max_supported", -1 );

    explosive = j.get_int( "explosive", -1 );

    sound_vol = j.get_int( "sound_vol", -1 );
    sound_fail_vol = j.get_int( "sound_fail_vol", -1 );

    collapse_radius = j.get_int( "collapse_radius", 1 );

    destroy_only = j.get_bool( "destroy_only", false );

    bash_below = j.get_bool( "bash_below", false );

    sound = to_translation( "smash!" );
    sound_fail = to_translation( "thump!" );
    j.read( "sound", sound );
    j.read( "sound_fail", sound_fail );

    switch( obj_type ) {
        case map_bash_info::furniture:
            furn_set = furn_str_id( j.get_string( "furn_set", "f_null" ) );
            break;
        case map_bash_info::terrain:
            ter_set = ter_str_id( j.get_string( "ter_set" ) );
            ter_set_bashed_from_above = ter_str_id( j.get_string( "ter_set_bashed_from_above",
                                                    ter_set.c_str() ) );
            break;
        case map_bash_info::field:
            fd_bash_move_cost = j.get_int( "move_cost", 100 );
            j.read( "msg_success", field_bash_msg_success );
            break;
    }

    if( j.has_member( "items" ) ) {
        drop_group = item_group::load_item_group( j.get_member( "items" ), "collection",
                     "map_bash_info for " + context );
    } else {
        drop_group = item_group_id( "EMPTY_GROUP" );
    }

    if( j.has_array( "tent_centers" ) ) {
        load_map_bash_tent_centers( j.get_array( "tent_centers" ), tent_centers );
    }

    return true;
}

map_deconstruct_info::map_deconstruct_info() : can_do( false ), deconstruct_above( false ),
    ter_set( ter_str_id::NULL_ID() ), furn_set( furn_str_id::NULL_ID() ) {}

bool map_deconstruct_info::load( const JsonObject &jsobj, const std::string &member,
                                 bool is_furniture, const std::string &context )
{
    if( !jsobj.has_object( member ) ) {
        return false;
    }
    JsonObject j = jsobj.get_object( member );
    furn_set = furn_str_id( j.get_string( "furn_set", "f_null" ) );

    if( !is_furniture ) {
        ter_set = ter_str_id( j.get_string( "ter_set" ) );
    }
    can_do = true;
    deconstruct_above = j.get_bool( "deconstruct_above", false );

    drop_group = item_group::load_item_group( j.get_member( "items" ), "collection",
                 "map_deconstruct_info for " + context );
    return true;
}

furn_workbench_info::furn_workbench_info() : multiplier( 1.0f ), allowed_mass( units::mass_max ),
    allowed_volume( units::volume_max ) {}

bool furn_workbench_info::load( const JsonObject &jsobj, const std::string &member )
{
    JsonObject j = jsobj.get_object( member );

    assign( j, "multiplier", multiplier );
    assign( j, "mass", allowed_mass );
    assign( j, "volume", allowed_volume );

    return true;
}

plant_data::plant_data() : transform( furn_str_id::NULL_ID() ), base( furn_str_id::NULL_ID() ),
    growth_multiplier( 1.0f ), harvest_multiplier( 1.0f ) {}

bool plant_data::load( const JsonObject &jsobj, const std::string &member )
{
    JsonObject j = jsobj.get_object( member );

    assign( j, "transform", transform );
    assign( j, "base", base );
    assign( j, "growth_multiplier", growth_multiplier );
    assign( j, "harvest_multiplier", harvest_multiplier );

    return true;
}

ter_t::ter_t() : open( ter_str_id::NULL_ID() ), close( ter_str_id::NULL_ID() ),
    transforms_into( ter_str_id::NULL_ID() ),
    roof( ter_str_id::NULL_ID() ), trap( tr_null ) {}

template<typename C, typename F>
void load_season_array( const JsonObject &jo, const std::string &key, C &container, F load_func )
{
    if( jo.has_string( key ) ) {
        container.fill( load_func( jo.get_string( key ) ) );

    } else if( jo.has_array( key ) ) {
        JsonArray arr = jo.get_array( key );
        if( arr.size() == 1 ) {
            container.fill( load_func( arr.get_string( 0 ) ) );

        } else if( arr.size() == container.size() ) {
            for( auto &e : container ) {
                e = load_func( arr.next_string() );
            }

        } else {
            jo.throw_error( "Incorrect number of entries", key );
        }

    } else {
        jo.throw_error( "Expected string or array", key );
    }
}

std::string map_data_common_t::name() const
{
    return name_.translated();
}

bool map_data_common_t::can_examine() const
{
    return !has_examine( iexamine::none );
}

bool map_data_common_t::has_examine( iexamine_function_ref func ) const
{
    return examine_func == &func;
}

bool map_data_common_t::has_examine( const std::string &action ) const
{
    return examine_actor->type == action;
}

void map_data_common_t::set_examine( iexamine_function_ref func )
{
    examine_func = &func;
}

void map_data_common_t::examine( player &guy, const tripoint &examp ) const
{
    if( !examine_actor ) {
        examine_func( guy, examp );
        return;
    }
    examine_actor->call( guy, examp );
}

void map_data_common_t::load_symbol( const JsonObject &jo )
{
    if( jo.has_member( "copy-from" ) && looks_like.empty() ) {
        looks_like = jo.get_string( "copy-from" );
    }
    jo.read( "looks_like", looks_like );

    load_season_array( jo, "symbol", symbol_, [&jo]( const std::string & str ) {
        if( str == "LINE_XOXO" ) {
            return LINE_XOXO;
        } else if( str == "LINE_OXOX" ) {
            return LINE_OXOX;
        } else if( str.length() != 1 ) {
            jo.throw_error( "Symbol string must be exactly 1 character long.", "symbol" );
        }
        return static_cast<int>( str[0] );
    } );

    const bool has_color = jo.has_member( "color" );
    const bool has_bgcolor = jo.has_member( "bgcolor" );
    if( has_color && has_bgcolor ) {
        jo.throw_error( "Found both color and bgcolor, only one of these is allowed." );
    } else if( has_color ) {
        load_season_array( jo, "color", color_, []( const std::string & str ) {
            // has to use a lambda because of default params
            return color_from_string( str );
        } );
    } else if( has_bgcolor ) {
        load_season_array( jo, "bgcolor", color_, bgcolor_from_string );
    } else {
        jo.throw_error( R"(Missing member: one of: "color", "bgcolor" must exist.)" );
    }
}

int map_data_common_t::symbol() const
{
    return symbol_[season_of_year( calendar::turn )];
}

nc_color map_data_common_t::color() const
{
    return color_[season_of_year( calendar::turn )];
}

const harvest_id &map_data_common_t::get_harvest() const
{
    return harvest_by_season[season_of_year( calendar::turn )];
}

const std::set<std::string> &map_data_common_t::get_harvest_names() const
{
    static const std::set<std::string> null_names = {};
    const harvest_id &hid = get_harvest();
    return hid.is_null() ? null_names : hid->names();
}

void load_furniture( const JsonObject &jo, const std::string &src )
{
    furniture_data.load( jo, src );
}

void load_terrain( const JsonObject &jo, const std::string &src )
{
    terrain_data.load( jo, src );
}

void map_data_common_t::set_flag( const std::string &flag )
{
    flags.insert( flag );
    const auto it = ter_bitflags_map.find( flag );
    if( it != ter_bitflags_map.end() ) {
        bitflags.set( it->second );
        if( !transparent && it->second == TFLAG_TRANSPARENT ) {
            transparent = true;
        }
        // wall connection check for JSON backwards compatibility
        if( it->second == TFLAG_WALL || it->second == TFLAG_CONNECT_TO_WALL ) {
            set_connects( "WALL" );
        }
    }
}

void map_data_common_t::set_connects( const std::string &connect_group_string )
{
    const auto it = ter_connects_map.find( connect_group_string );
    if( it != ter_connects_map.end() ) {
        connect_group = it->second;
    } else { // arbitrary connect groups are a bad idea for optimization reasons
        debugmsg( "can't find terrain connection group %s", connect_group_string.c_str() );
    }
}

bool map_data_common_t::connects( int &ret ) const
{
    if( connect_group != TERCONN_NONE ) {
        ret = connect_group;
        return true;
    }
    return false;
}

ter_id t_null;
const ter_str_id t_str_null( "t_null" );
// Real nothingness; makes you fall a z-level
const ter_str_id t_hole( "t_hole" );
// Ground
const ter_str_id t_dirt( "t_dirt" );
const ter_str_id t_sand( "t_sand" );
const ter_str_id t_clay( "t_clay" );
const ter_str_id t_dirtmound( "t_dirtmound" );
const ter_str_id t_pit_shallow( "t_pit_shallow" );
const ter_str_id t_pit( "t_pit" );
const ter_str_id t_grave( "t_grave" );
const ter_str_id t_grave_new( "t_grave_new" );
const ter_str_id t_pit_corpsed( "t_pit_corpsed" );
const ter_str_id t_pit_covered( "t_pit_covered" );
const ter_str_id t_pit_spiked( "t_pit_spiked" );
const ter_str_id t_pit_spiked_covered( "t_pit_spiked_covered" );
const ter_str_id t_pit_glass( "t_pit_glass" );
const ter_str_id t_pit_glass_covered( "t_pit_glass_covered" );
const ter_str_id t_rock_floor( "t_rock_floor" );
const ter_str_id t_grass( "t_grass" );
const ter_str_id t_grass_long( "t_grass_long" );
const ter_str_id t_grass_tall( "t_grass_tall" );
const ter_str_id t_grass_golf( "t_grass_golf" );
const ter_str_id t_grass_dead( "t_grass_dead" );
const ter_str_id t_grass_white( "t_grass_white" );
const ter_str_id t_moss( "t_moss" );
const ter_str_id t_metal_floor( "t_metal_floor" );
const ter_str_id t_pavement( "t_pavement" );
const ter_str_id t_pavement_y( "t_pavement_y" );
const ter_str_id t_sidewalk( "t_sidewalk" );
const ter_str_id t_concrete( "t_concrete" );
const ter_str_id t_zebra( "t_zebra" );
const ter_str_id t_thconc_floor( "t_thconc_floor" );
const ter_str_id t_thconc_floor_olight( "t_thconc_floor_olight" );
const ter_str_id t_strconc_floor( "t_strconc_floor" );
const ter_str_id t_floor( "t_floor" );
const ter_str_id t_floor_waxed( "t_floor_waxed" );
//Dirt floor(Has roof)
const ter_str_id t_dirtfloor( "t_dirtfloor" );
const ter_str_id t_carpet_red( "t_carpet_red" );
const ter_str_id t_carpet_yellow( "t_carpet_yellow" );
const ter_str_id t_carpet_purple( "t_carpet_purple" );
const ter_str_id t_carpet_green( "t_carpet_green" );
const ter_str_id t_linoleum_white( "t_linoleum_white" );
const ter_str_id t_linoleum_gray( "t_linoleum_gray" );
const ter_str_id t_grate( "t_grate" );
const ter_str_id t_slime( "t_slime" );
const ter_str_id t_bridge( "t_bridge" );
const ter_str_id t_covered_well( "t_covered_well" );
// Lighting related
const ter_str_id t_utility_light( "t_utility_light" );
// Walls
const ter_str_id t_wall_log_half( "t_wall_log_half" );
const ter_str_id t_wall_log( "t_wall_log" );
const ter_str_id t_wall_log_chipped( "t_wall_log_chipped" );
const ter_str_id t_wall_log_broken( "t_wall_log_broken" );
const ter_str_id t_palisade( "t_palisade" );
const ter_str_id t_palisade_gate( "t_palisade_gate" );
const ter_str_id t_palisade_gate_o( "t_palisade_gate_o" );
const ter_str_id t_wall_half( "t_wall_half" );
const ter_str_id t_wall_wood( "t_wall_wood" );
const ter_str_id t_wall_wood_chipped( "t_wall_wood_chipped" );
const ter_str_id t_wall_wood_broken( "t_wall_wood_broken" );
const ter_str_id t_wall( "t_wall" );
const ter_str_id t_concrete_wall( "t_concrete_wall" );
const ter_str_id t_brick_wall( "t_brick_wall" );
const ter_str_id t_wall_metal( "t_wall_metal" );
const ter_str_id t_scrap_wall( "t_scrap_wall" );
const ter_str_id t_scrap_wall_halfway( "t_scrap_wall_halfway" );
const ter_str_id t_wall_glass( "t_wall_glass" );
const ter_str_id t_wall_glass_alarm( "t_wall_glass_alarm" );
const ter_str_id t_reinforced_glass( "t_reinforced_glass" );
const ter_str_id t_reinforced_glass_shutter( "t_reinforced_glass_shutter" );
const ter_str_id t_reinforced_glass_shutter_open( "t_reinforced_glass_shutter_open" );
const ter_str_id t_laminated_glass( "t_laminated_glass" );
const ter_str_id t_ballistic_glass( "t_ballistic_glass" );
const ter_str_id t_reinforced_door_glass_o( "t_reinforced_door_glass_o" );
const ter_str_id t_reinforced_door_glass_c( "t_reinforced_door_glass_c" );
const ter_str_id t_bars( "t_bars" );
const ter_str_id t_reb_cage( "t_reb_cage" );
const ter_str_id t_door_c( "t_door_c" );
const ter_str_id t_door_c_peep( "t_door_c_peep" );
const ter_str_id t_door_b( "t_door_b" );
const ter_str_id t_door_b_peep( "t_door_b_peep" );
const ter_str_id t_door_o( "t_door_o" );
const ter_str_id t_door_o_peep( "t_door_o_peep" );
const ter_str_id t_rdoor_c( "t_rdoor_c" );
const ter_str_id t_rdoor_b( "t_rdoor_b" );
const ter_str_id t_rdoor_o( "t_rdoor_o" );
const ter_str_id t_door_locked_interior( "t_door_locked_interior" );
const ter_str_id t_door_locked( "t_door_locked" );
const ter_str_id t_door_locked_peep( "t_door_locked_peep" );
const ter_str_id t_door_locked_alarm( "t_door_locked_alarm" );
const ter_str_id t_door_frame( "t_door_frame" );
const ter_str_id t_chaingate_l( "t_chaingate_l" );
const ter_str_id t_fencegate_c( "t_fencegate_c" );
const ter_str_id t_fencegate_o( "t_fencegate_o" );
const ter_str_id t_chaingate_c( "t_chaingate_c" );
const ter_str_id t_chaingate_o( "t_chaingate_o" );
const ter_str_id t_retractable_gate_c( "t_retractable_gate_c" );
const ter_str_id t_retractable_gate_l( "t_retractable_gate_l" );
const ter_str_id t_retractable_gate_o( "t_retractable_gate_o" );
const ter_str_id t_door_boarded( "t_door_boarded" );
const ter_str_id t_door_boarded_damaged( "t_door_boarded_damaged" );
const ter_str_id t_door_boarded_peep( "t_door_boarded_peep" );
const ter_str_id t_rdoor_boarded( "t_rdoor_boarded" );
const ter_str_id t_rdoor_boarded_damaged( "t_rdoor_boarded_damaged" );
const ter_str_id t_door_boarded_damaged_peep( "t_door_boarded_damaged_peep" );
const ter_str_id t_door_metal_c( "t_door_metal_c" );
const ter_str_id t_door_metal_o( "t_door_metal_o" );
const ter_str_id t_door_metal_locked( "t_door_metal_locked" );
const ter_str_id t_door_metal_pickable( "t_door_metal_pickable" );
const ter_str_id t_mdoor_frame( "t_mdoor_frame" );
const ter_str_id t_door_bar_c( "t_door_bar_c" );
const ter_str_id t_door_bar_o( "t_door_bar_o" );
const ter_str_id t_door_bar_locked( "t_door_bar_locked" );
const ter_str_id t_door_glass_c( "t_door_glass_c" );
const ter_str_id t_door_glass_o( "t_door_glass_o" );
const ter_str_id t_door_glass_frosted_c( "t_door_glass_frosted_c" );
const ter_str_id t_door_glass_frosted_o( "t_door_glass_frosted_o" );
const ter_str_id t_portcullis( "t_portcullis" );
const ter_str_id t_recycler( "t_recycler" );
const ter_str_id t_window( "t_window" );
const ter_str_id t_window_taped( "t_window_taped" );
const ter_str_id t_window_domestic( "t_window_domestic" );
const ter_str_id t_window_domestic_taped( "t_window_domestic_taped" );
const ter_str_id t_window_open( "t_window_open" );
const ter_str_id t_curtains( "t_curtains" );
const ter_str_id t_window_bars_curtains( "t_window_bars_curtains" );
const ter_str_id t_window_bars_domestic( "t_window_bars_domestic" );
const ter_str_id t_window_alarm( "t_window_alarm" );
const ter_str_id t_window_alarm_taped( "t_window_alarm_taped" );
const ter_str_id t_window_empty( "t_window_empty" );
const ter_str_id t_window_frame( "t_window_frame" );
const ter_str_id t_window_boarded( "t_window_boarded" );
const ter_str_id t_window_boarded_noglass( "t_window_boarded_noglass" );
const ter_str_id t_window_reinforced( "t_window_reinforced" );
const ter_str_id t_window_reinforced_noglass( "t_window_reinforced_noglass" );
const ter_str_id t_window_enhanced( "t_window_enhanced" );
const ter_str_id t_window_enhanced_noglass( "t_window_enhanced_noglass" );
const ter_str_id t_window_bars_alarm( "t_window_bars_alarm" );
const ter_str_id t_window_bars( "t_window_bars" );
const ter_str_id t_metal_grate_window( "t_metal_grate_window" );
const ter_str_id t_metal_grate_window_with_curtain( "t_metal_grate_window_with_curtain" );
const ter_str_id t_metal_grate_window_with_curtain_open( "t_metal_grate_window_with_curtain_open" );
const ter_str_id t_metal_grate_window_noglass( "t_metal_grate_window_noglass" );
const ter_str_id
t_metal_grate_window_with_curtain_noglass( "t_metal_grate_window_with_curtain_noglass" );
const ter_str_id
t_metal_grate_window_with_curtain_open_noglass( "t_metal_grate_window_with_curtain_open_noglass" );
const ter_str_id t_window_stained_green( "t_window_stained_green" );
const ter_str_id t_window_stained_red( "t_window_stained_red" );
const ter_str_id t_window_stained_blue( "t_window_stained_blue" );
const ter_str_id t_window_no_curtains( "t_window_no_curtains" );
const ter_str_id t_window_no_curtains_open( "t_window_no_curtains_open" );
const ter_str_id t_window_no_curtains_taped( "t_window_no_curtains_taped" );
const ter_str_id t_rock( "t_rock" );
const ter_str_id t_fault( "t_fault" );
const ter_str_id t_paper( "t_paper" );
const ter_str_id t_rock_wall( "t_rock_wall" );
const ter_str_id t_rock_wall_half( "t_rock_wall_half" );
// Tree
const ter_str_id t_tree( "t_tree" );
const ter_str_id t_tree_young( "t_tree_young" );
const ter_str_id t_tree_apple( "t_tree_apple" );
const ter_str_id t_tree_apple_harvested( "t_tree_apple_harvested" );
const ter_str_id t_tree_coffee( "t_tree_coffee" );
const ter_str_id t_tree_coffee_harvested( "t_tree_coffee_harvested" );
const ter_str_id t_tree_pear( "t_tree_pear" );
const ter_str_id t_tree_pear_harvested( "t_tree_pear_harvested" );
const ter_str_id t_tree_cherry( "t_tree_cherry" );
const ter_str_id t_tree_cherry_harvested( "t_tree_cherry_harvested" );
const ter_str_id t_tree_peach( "t_tree_peach" );
const ter_str_id t_tree_peach_harvested( "t_tree_peach_harvested" );
const ter_str_id t_tree_apricot( "t_tree_apricot" );
const ter_str_id t_tree_apricot_harvested( "t_tree_apricot_harvested" );
const ter_str_id t_tree_plum( "t_tree_plum" );
const ter_str_id t_tree_plum_harvested( "t_tree_plum_harvested" );
const ter_str_id t_tree_pine( "t_tree_pine" );
const ter_str_id t_tree_blackjack( "t_tree_blackjack" );
const ter_str_id t_tree_birch( "t_tree_birch" );
const ter_str_id t_tree_willow( "t_tree_willow" );
const ter_str_id t_tree_maple( "t_tree_maple" );
const ter_str_id t_tree_maple_tapped( "t_tree_maple_tapped" );
const ter_str_id t_tree_hickory( "t_tree_hickory" );
const ter_str_id t_tree_hickory_dead( "t_tree_hickory_dead" );
const ter_str_id t_tree_hickory_harvested( "t_tree_hickory_harvested" );
const ter_str_id t_tree_deadpine( "t_tree_deadpine" );
const ter_str_id t_underbrush( "t_underbrush" );
const ter_str_id t_shrub( "t_shrub" );
const ter_str_id t_shrub_blueberry( "t_shrub_blueberry" );
const ter_str_id t_shrub_strawberry( "t_shrub_strawberry" );
const ter_str_id t_trunk( "t_trunk" );
const ter_str_id t_stump( "t_stump" );
const ter_str_id t_root_wall( "t_root_wall" );
const ter_str_id t_wax( "t_wax" );
const ter_str_id t_floor_wax( "t_floor_wax" );
const ter_str_id t_fence( "t_fence" );
const ter_str_id t_chainfence( "t_chainfence" );
const ter_str_id t_chainfence_posts( "t_chainfence_posts" );
const ter_str_id t_fence_post( "t_fence_post" );
const ter_str_id t_fence_wire( "t_fence_wire" );
const ter_str_id t_fence_barbed( "t_fence_barbed" );
const ter_str_id t_fence_rope( "t_fence_rope" );
const ter_str_id t_railing( "t_railing" );
// Nether
const ter_str_id t_marloss( "t_marloss" );
const ter_str_id t_fungus_floor_in( "t_fungus_floor_in" );
const ter_str_id t_fungus_floor_sup( "t_fungus_floor_sup" );
const ter_str_id t_fungus_floor_out( "t_fungus_floor_out" );
const ter_str_id t_fungus_wall( "t_fungus_wall" );
const ter_str_id t_fungus_mound( "t_fungus_mound" );
const ter_str_id t_fungus( "t_fungus" );
const ter_str_id t_shrub_fungal( "t_shrub_fungal" );
const ter_str_id t_tree_fungal( "t_tree_fungal" );
const ter_str_id t_tree_fungal_young( "t_tree_fungal_young" );
const ter_str_id t_marloss_tree( "t_marloss_tree" );
// Water, lava, etc.
const ter_str_id t_water_moving_dp( "t_water_moving_dp" );
const ter_str_id t_water_moving_sh( "t_water_moving_sh" );
const ter_str_id t_water_sh( "t_water_sh" );
const ter_str_id t_water_dp( "t_water_dp" );
const ter_str_id t_swater_sh( "t_swater_sh" );
const ter_str_id t_swater_dp( "t_swater_dp" );
const ter_str_id t_water_pool( "t_water_pool" );
const ter_str_id t_sewage( "t_sewage" );
const ter_str_id t_lava( "t_lava" );
// More embellishments than you can shake a stick at.
const ter_str_id t_sandbox( "t_sandbox" );
const ter_str_id t_slide( "t_slide" );
const ter_str_id t_monkey_bars( "t_monkey_bars" );
const ter_str_id t_backboard( "t_backboard" );
const ter_str_id t_gas_pump( "t_gas_pump" );
const ter_str_id t_gas_pump_smashed( "t_gas_pump_smashed" );
const ter_str_id t_diesel_pump( "t_diesel_pump" );
const ter_str_id t_diesel_pump_smashed( "t_diesel_pump_smashed" );
const ter_str_id t_atm( "t_atm" );
const ter_str_id t_generator_broken( "t_generator_broken" );
const ter_str_id t_missile( "t_missile" );
const ter_str_id t_missile_exploded( "t_missile_exploded" );
const ter_str_id t_radio_tower( "t_radio_tower" );
const ter_str_id t_radio_controls( "t_radio_controls" );
const ter_str_id t_console_broken( "t_console_broken" );
const ter_str_id t_console( "t_console" );
const ter_str_id t_gates_mech_control( "t_gates_mech_control" );
const ter_str_id t_gates_control_concrete( "t_gates_control_concrete" );
const ter_str_id t_gates_control_brick( "t_gates_control_brick" );
const ter_str_id t_barndoor( "t_barndoor" );
const ter_str_id t_palisade_pulley( "t_palisade_pulley" );
const ter_str_id t_gates_control_metal( "t_gates_control_metal" );
const ter_str_id t_sewage_pipe( "t_sewage_pipe" );
const ter_str_id t_sewage_pump( "t_sewage_pump" );
const ter_str_id t_centrifuge( "t_centrifuge" );
const ter_str_id t_column( "t_column" );
const ter_str_id t_vat( "t_vat" );
const ter_str_id t_rootcellar( "t_rootcellar" );
const ter_str_id t_cvdbody( "t_cvdbody" );
const ter_str_id t_cvdmachine( "t_cvdmachine" );
const ter_str_id t_water_pump( "t_water_pump" );
const ter_str_id t_conveyor( "t_conveyor" );
const ter_str_id t_machinery_light( "t_machinery_light" );
const ter_str_id t_machinery_heavy( "t_machinery_heavy" );
const ter_str_id t_machinery_old( "t_machinery_old" );
const ter_str_id t_machinery_electronic( "t_machinery_electronic" );
const ter_str_id t_improvised_shelter( "t_improvised_shelter" );
// Staircases etc.
const ter_str_id t_stairs_down( "t_stairs_down" );
const ter_str_id t_stairs_up( "t_stairs_up" );
const ter_str_id t_manhole( "t_manhole" );
const ter_str_id t_ladder_up( "t_ladder_up" );
const ter_str_id t_ladder_down( "t_ladder_down" );
const ter_str_id t_slope_down( "t_slope_down" );
const ter_str_id t_slope_up( "t_slope_up" );
const ter_str_id t_rope_up( "t_rope_up" );
const ter_str_id t_manhole_cover( "t_manhole_cover" );
// Special
const ter_str_id t_card_science( "t_card_science" );
const ter_str_id t_card_military( "t_card_military" );
const ter_str_id t_card_industrial( "t_card_industrial" );
const ter_str_id t_card_reader_broken( "t_card_reader_broken" );
const ter_str_id t_slot_machine( "t_slot_machine" );
const ter_str_id t_elevator_control( "t_elevator_control" );
const ter_str_id t_elevator_control_off( "t_elevator_control_off" );
const ter_str_id t_elevator( "t_elevator" );
const ter_str_id t_pedestal_wyrm( "t_pedestal_wyrm" );
const ter_str_id t_pedestal_temple( "t_pedestal_temple" );
// Temple tiles
const ter_str_id t_rock_red( "t_rock_red" );
const ter_str_id t_rock_green( "t_rock_green" );
const ter_str_id t_rock_blue( "t_rock_blue" );
const ter_str_id t_floor_red( "t_floor_red" );
const ter_str_id t_floor_green( "t_floor_green" );
const ter_str_id t_floor_blue( "t_floor_blue" );
const ter_str_id t_switch_rg( "t_switch_rg" );
const ter_str_id t_switch_gb( "t_switch_gb" );
const ter_str_id t_switch_rb( "t_switch_rb" );
const ter_str_id t_switch_even( "t_switch_even" );
const ter_str_id t_open_air( "t_open_air" );
const ter_str_id t_plut_generator( "t_plut_generator" );
const ter_str_id t_pavement_bg_dp( "t_pavement_bg_dp" );
const ter_str_id t_pavement_y_bg_dp( "t_pavement_y_bg_dp" );
const ter_str_id t_sidewalk_bg_dp( "t_sidewalk_bg_dp" );
const ter_str_id t_guardrail_bg_dp( "t_guardrail_bg_dp" );
const ter_str_id t_rad_platform( "t_rad_platform" );
// Railroad and subway
const ter_str_id t_railroad_rubble( "t_railroad_rubble" );
const ter_str_id t_buffer_stop( "t_buffer_stop" );
const ter_str_id t_railroad_crossing_signal( "t_railroad_crossing_signal" );
const ter_str_id t_crossbuck_wood( "t_crossbuck_wood" );
const ter_str_id t_crossbuck_metal( "t_crossbuck_metal" );
const ter_str_id t_railroad_tie( "t_railroad_tie" );
const ter_str_id t_railroad_tie_h( "t_railroad_tie_h" );
const ter_str_id t_railroad_tie_v( "t_railroad_tie_v" );
const ter_str_id t_railroad_tie_d( "t_railroad_tie_d" );
const ter_str_id t_railroad_track( "t_railroad_track" );
const ter_str_id t_railroad_track_h( "t_railroad_track_h" );
const ter_str_id t_railroad_track_v( "t_railroad_track_v" );
const ter_str_id t_railroad_track_d( "t_railroad_track_d" );
const ter_str_id t_railroad_track_d1( "t_railroad_track_d1" );
const ter_str_id t_railroad_track_d2( "t_railroad_track_d2" );
const ter_str_id t_railroad_track_on_tie( "t_railroad_track_on_tie" );
const ter_str_id t_railroad_track_h_on_tie( "t_railroad_track_h_on_tie" );
const ter_str_id t_railroad_track_v_on_tie( "t_railroad_track_v_on_tie" );
const ter_str_id t_railroad_track_d_on_tie( "t_railroad_track_d_on_tie" );

// TODO: Put this crap into an inclusion, which should be generated automatically using JSON data

void set_ter_ids()
{
    t_null = ter_id( "t_null" );

    for( const ter_t &elem : terrain_data.get_all() ) {
        ter_t &ter = const_cast<ter_t &>( elem );
        if( ter.trap_id_str.empty() ) {
            ter.trap = tr_null;
        } else {
            ter.trap = trap_str_id( ter.trap_id_str );
        }
    }
}

void reset_furn_ter()
{
    terrain_data.reset();
    furniture_data.reset();
}

furn_id f_null;
const furn_str_id f_str_null( "f_null" );
const furn_str_id f_hay( "f_hay" );
const furn_str_id f_rubble( "f_rubble" );
const furn_str_id f_rubble_rock( "f_rubble_rock" );
const furn_str_id f_wreckage( "f_wreckage" );
const furn_str_id f_ash( "f_ash" );
const furn_str_id f_barricade_road( "f_barricade_road" );
const furn_str_id f_sandbag_half( "f_sandbag_half" );
const furn_str_id f_sandbag_wall( "f_sandbag_wall" );
const furn_str_id f_bulletin( "f_bulletin" );
const furn_str_id f_indoor_plant( "f_indoor_plant" );
const furn_str_id f_bed( "f_bed" );
const furn_str_id f_toilet( "f_toilet" );
const furn_str_id f_makeshift_bed( "f_makeshift_bed" );
const furn_str_id f_straw_bed( "f_straw_bed" );
const furn_str_id f_sink( "f_sink" );
const furn_str_id f_oven( "f_oven" );
const furn_str_id f_woodstove( "f_woodstove" );
const furn_str_id f_fireplace( "f_fireplace" );
const furn_str_id f_bathtub( "f_bathtub" );
const furn_str_id f_chair( "f_chair" );
const furn_str_id f_armchair( "f_armchair" );
const furn_str_id f_sofa( "f_sofa" );
const furn_str_id f_cupboard( "f_cupboard" );
const furn_str_id f_trashcan( "f_trashcan" );
const furn_str_id f_desk( "f_desk" );
const furn_str_id f_exercise( "f_exercise" );
const furn_str_id f_bench( "f_bench" );
const furn_str_id f_table( "f_table" );
const furn_str_id f_pool_table( "f_pool_table" );
const furn_str_id f_counter( "f_counter" );
const furn_str_id f_fridge( "f_fridge" );
const furn_str_id f_glass_fridge( "f_glass_fridge" );
const furn_str_id f_dresser( "f_dresser" );
const furn_str_id f_locker( "f_locker" );
const furn_str_id f_rack( "f_rack" );
const furn_str_id f_bookcase( "f_bookcase" );
const furn_str_id f_washer( "f_washer" );
const furn_str_id f_dryer( "f_dryer" );
const furn_str_id f_vending_c( "f_vending_c" );
const furn_str_id f_vending_o( "f_vending_o" );
const furn_str_id f_dumpster( "f_dumpster" );
const furn_str_id f_dive_block( "f_dive_block" );
const furn_str_id f_crate_c( "f_crate_c" );
const furn_str_id f_crate_o( "f_crate_o" );
const furn_str_id f_coffin_c( "f_coffin_c" );
const furn_str_id f_coffin_o( "f_coffin_o" );
const furn_str_id f_gunsafe_ml( "f_gunsafe_ml" );
const furn_str_id f_gunsafe_mj( "f_gunsafe_mj" );
const furn_str_id f_gun_safe_el( "f_gun_safe_el" );
const furn_str_id f_large_canvas_wall( "f_large_canvas_wall" );
const furn_str_id f_canvas_wall( "f_canvas_wall" );
const furn_str_id f_canvas_door( "f_canvas_door" );
const furn_str_id f_canvas_door_o( "f_canvas_door_o" );
const furn_str_id f_groundsheet( "f_groundsheet" );
const furn_str_id f_fema_groundsheet( "f_fema_groundsheet" );
const furn_str_id f_large_groundsheet( "f_large_groundsheet" );
const furn_str_id f_large_canvas_door( "f_large_canvas_door" );
const furn_str_id f_large_canvas_door_o( "f_large_canvas_door_o" );
const furn_str_id f_center_groundsheet( "f_center_groundsheet" );
const furn_str_id f_skin_wall( "f_skin_wall" );
const furn_str_id f_skin_door( "f_skin_door" );
const furn_str_id f_skin_door_o( "f_skin_door_o" );
const furn_str_id f_skin_groundsheet( "f_skin_groundsheet" );
const furn_str_id f_mutpoppy( "f_mutpoppy" );
const furn_str_id f_flower_fungal( "f_flower_fungal" );
const furn_str_id f_fungal_mass( "f_fungal_mass" );
const furn_str_id f_fungal_clump( "f_fungal_clump" );
const furn_str_id f_cattails( "f_cattails" );
const furn_str_id f_lotus( "f_lotus" );
const furn_str_id f_lilypad( "f_lilypad" );
const furn_str_id f_safe_c( "f_safe_c" );
const furn_str_id f_safe_l( "f_safe_l" );
const furn_str_id f_safe_o( "f_safe_o" );
const furn_str_id f_plant_seed( "f_plant_seed" );
const furn_str_id f_plant_seedling( "f_plant_seedling" );
const furn_str_id f_plant_mature( "f_plant_mature" );
const furn_str_id f_plant_harvest( "f_plant_harvest" );
const furn_str_id f_fvat_empty( "f_fvat_empty" );
const furn_str_id f_fvat_full( "f_fvat_full" );
const furn_str_id f_wood_keg( "f_wood_keg" );
const furn_str_id f_standing_tank( "f_standing_tank" );
const furn_str_id f_egg_sackbw( "f_egg_sackbw" );
const furn_str_id f_egg_sackcs( "f_egg_sackcs" );
const furn_str_id f_egg_sackws( "f_egg_sackws" );
const furn_str_id f_egg_sacke( "f_egg_sacke" );
const furn_str_id f_flower_marloss( "f_flower_marloss" );
const furn_str_id f_tatami( "f_tatami" );
const furn_str_id f_kiln_empty( "f_kiln_empty" );
const furn_str_id f_kiln_full( "f_kiln_full" );
const furn_str_id f_kiln_metal_empty( "f_kiln_metal_empty" );
const furn_str_id f_kiln_metal_full( "f_kiln_metal_full" );
const furn_str_id f_arcfurnace_empty( "f_arcfurnace_empty" );
const furn_str_id f_arcfurnace_full( "f_arcfurnace_full" );
const furn_str_id f_smoking_rack( "f_smoking_rack" );
const furn_str_id f_smoking_rack_active( "f_smoking_rack_active" );
const furn_str_id f_metal_smoking_rack( "f_metal_smoking_rack" );
const furn_str_id f_metal_smoking_rack_active( "f_metal_smoking_rack_active" );
const furn_str_id f_water_mill( "f_water_mill" );
const furn_str_id f_water_mill_active( "f_water_mill_active" );
const furn_str_id f_wind_mill( "f_wind_mill" );
const furn_str_id f_wind_mill_active( "f_wind_mill_active" );
const furn_str_id f_robotic_arm( "f_robotic_arm" );
const furn_str_id f_vending_reinforced( "f_vending_reinforced" );
const furn_str_id f_brazier( "f_brazier" );
const furn_str_id f_firering( "f_firering" );
const furn_str_id f_tourist_table( "f_tourist_table" );
const furn_str_id f_camp_chair( "f_camp_chair" );
const furn_str_id f_sign( "f_sign" );
const furn_str_id f_street_light( "f_street_light" );
const furn_str_id f_traffic_light( "f_traffic_light" );
const furn_str_id f_console( "f_console" );
const furn_str_id f_console_broken( "f_console_broken" );

void set_furn_ids()
{
    f_null = furn_id( "f_null" );
}

size_t ter_t::count()
{
    return terrain_data.size();
}

namespace io
{
template<>
std::string enum_to_string<season_type>( season_type data )
{
    switch( data ) {
        // *INDENT-OFF*
        case season_type::SPRING: return "spring";
        case season_type::SUMMER: return "summer";
        case season_type::AUTUMN: return "autumn";
        case season_type::WINTER: return "winter";
        // *INDENT-ON*
        case season_type::NUM_SEASONS:
            break;
    }
    debugmsg( "Invalid season_type" );
    abort();
}
} // namespace io

static std::map<std::string, cata::clone_ptr<iexamine_actor>> examine_actors;

static void add_actor( std::unique_ptr<iexamine_actor> ptr )
{
    std::string type = ptr->type;
    examine_actors[type] = cata::clone_ptr<iexamine_actor>( std::move( ptr ) );
}

static cata::clone_ptr<iexamine_actor> iexamine_actor_from_jsobj( const JsonObject &jo )
{
    std::string type = jo.get_string( "type" );
    try {
        return examine_actors.at( type );
    } catch( const std::exception & ) {
        jo.throw_error( string_format( "Invalid iexamine actor %s", type ) );
    }
}

void init_mapdata()
{
    add_actor( std::make_unique<cardreader_examine_actor>() );
}

void map_data_common_t::load( const JsonObject &jo, const std::string & )
{
    if( jo.has_string( "examine_action" ) ) {
        examine_func = iexamine_function_from_string( jo.get_string( "examine_action" ) );
    } else if( jo.has_object( "examine_action" ) ) {
        JsonObject data = jo.get_object( "examine_action" );
        examine_actor = iexamine_actor_from_jsobj( data );
        examine_actor->load( data );
    } else {
        examine_func = iexamine_function_from_string( "none" );
    }

    if( jo.has_array( "harvest_by_season" ) ) {
        for( JsonObject harvest_jo : jo.get_array( "harvest_by_season" ) ) {
            auto season_strings = harvest_jo.get_tags( "seasons" );
            std::set<season_type> seasons;
            std::transform( season_strings.begin(), season_strings.end(), std::inserter( seasons,
                            seasons.begin() ), io::string_to_enum<season_type> );

            harvest_id hl;
            harvest_jo.read( "id", hl );

            for( season_type s : seasons ) {
                harvest_by_season[ s ] = hl;
            }
        }
    }

    mandatory( jo, was_loaded, "description", description );
    optional( jo, was_loaded, "curtain_transform", curtain_transform );
}

void ter_t::load( const JsonObject &jo, const std::string &src )
{
    map_data_common_t::load( jo, src );
    mandatory( jo, was_loaded, "name", name_ );
    mandatory( jo, was_loaded, "move_cost", movecost );
    optional( jo, was_loaded, "coverage", coverage );
    assign( jo, "max_volume", max_volume, src == "dda" );
    optional( jo, was_loaded, "trap", trap_id_str );
    optional( jo, was_loaded, "heat_radiation", heat_radiation );

    optional( jo, was_loaded, "light_emitted", light_emitted );

    load_symbol( jo );

    trap = tr_null;
    transparent = false;
    connect_group = TERCONN_NONE;

    for( auto &flag : jo.get_string_array( "flags" ) ) {
        set_flag( flag );
    }
    // connect_group is initialized to none, then terrain flags are set, then finally
    // connections from JSON are set. This is so that wall flags can set wall connections
    // but can be overridden by explicit connections in JSON.
    if( jo.has_member( "connects_to" ) ) {
        set_connects( jo.get_string( "connects_to" ) );
    }

    optional( jo, was_loaded, "allowed_template_ids", allowed_template_id );

    optional( jo, was_loaded, "open", open, ter_str_id::NULL_ID() );
    optional( jo, was_loaded, "close", close, ter_str_id::NULL_ID() );
    optional( jo, was_loaded, "transforms_into", transforms_into, ter_str_id::NULL_ID() );
    optional( jo, was_loaded, "roof", roof, ter_str_id::NULL_ID() );

    optional( jo, was_loaded, "emissions", emissions );

    bash.load( jo, "bash", map_bash_info::terrain, "terrain " + id.str() );
    deconstruct.load( jo, "deconstruct", false, "terrain " + id.str() );
}

static void check_bash_items( const map_bash_info &mbi, const std::string &id, bool is_terrain )
{
    if( !item_group::group_is_defined( mbi.drop_group ) ) {
        debugmsg( "%s: bash result item group %s does not exist", id.c_str(), mbi.drop_group.c_str() );
    }
    if( mbi.str_max != -1 ) {
        if( is_terrain && mbi.ter_set.is_empty() ) { // Some tiles specify t_null explicitly
            debugmsg( "bash result terrain of %s is undefined/empty", id.c_str() );
        }
        if( !mbi.ter_set.is_valid() ) {
            debugmsg( "bash result terrain %s of %s does not exist", mbi.ter_set.c_str(), id.c_str() );
        }
        if( !mbi.furn_set.is_valid() ) {
            debugmsg( "bash result furniture %s of %s does not exist", mbi.furn_set.c_str(), id.c_str() );
        }
    }
}

static void check_decon_items( const map_deconstruct_info &mbi, const std::string &id,
                               bool is_terrain )
{
    if( !mbi.can_do ) {
        return;
    }
    if( !item_group::group_is_defined( mbi.drop_group ) ) {
        debugmsg( "%s: deconstruct result item group %s does not exist", id.c_str(),
                  mbi.drop_group.c_str() );
    }
    if( is_terrain && mbi.ter_set.is_empty() ) { // Some tiles specify t_null explicitly
        debugmsg( "deconstruct result terrain of %s is undefined/empty", id.c_str() );
    }
    if( !mbi.ter_set.is_valid() ) {
        debugmsg( "deconstruct result terrain %s of %s does not exist", mbi.ter_set.c_str(), id.c_str() );
    }
    if( !mbi.furn_set.is_valid() ) {
        debugmsg( "deconstruct result furniture %s of %s does not exist", mbi.furn_set.c_str(),
                  id.c_str() );
    }
}

void ter_t::check() const
{
    map_data_common_t::check();
    check_bash_items( bash, id.str(), true );
    check_decon_items( deconstruct, id.str(), true );

    if( !transforms_into.is_valid() ) {
        debugmsg( "invalid transforms_into %s for %s", transforms_into.c_str(), id.c_str() );
    }

    // Validate open/close transforms
    if( !open.is_valid() ) {
        debugmsg( "invalid terrain %s for opening %s", open.c_str(), id.c_str() );
    }
    if( !close.is_valid() ) {
        debugmsg( "invalid terrain %s for closing %s", close.c_str(), id.c_str() );
    }
    // Check transition consistency for opening/closing terrain. Has an obvious
    // exception for locked terrains - those aren't expected to be locked again
    if( open && open->close && open->close != id && !has_flag( flag_LOCKED ) ) {
        debugmsg( "opening terrain %s for %s doesn't reciprocate", open.c_str(), id.c_str() );
    }
    if( close && close->open && close->open != id && !has_flag( flag_LOCKED ) ) {
        debugmsg( "closing terrain %s for %s doesn't reciprocate", close.c_str(), id.c_str() );
    }

    // Validate curtain transforms
    if( has_examine( iexamine::curtains ) && !has_curtains() ) {
        debugmsg( "%s is a curtain, but has no curtain_transform", id.c_str() );
    }
    if( !has_examine( iexamine::curtains ) && has_curtains() ) {
        debugmsg( "%s is not a curtain, but has curtain_transform", id.c_str() );
    }
    if( !curtain_transform.is_empty() && !curtain_transform.is_valid() ) {
        debugmsg( "%s has invalid curtain transform target %s", id.c_str(), curtain_transform.c_str() );
    }

    // Validate generic transforms
    if( transforms_into && transforms_into == id ) {
        debugmsg( "%s transforms_into itself", id.c_str() );
    }

    for( const emit_id &e : emissions ) {
        if( !e.is_valid() ) {
            debugmsg( "ter %s has invalid emission %s set", id.c_str(), e.str().c_str() );
        }
    }
}

furn_t::furn_t() : open( furn_str_id::NULL_ID() ), close( furn_str_id::NULL_ID() ) {}

size_t furn_t::count()
{
    return furniture_data.size();
}

bool furn_t::is_movable() const
{
    return move_str_req >= 0;
}

void furn_t::load( const JsonObject &jo, const std::string &src )
{
    map_data_common_t::load( jo, src );
    mandatory( jo, was_loaded, "name", name_ );
    mandatory( jo, was_loaded, "move_cost_mod", movecost );
    optional( jo, was_loaded, "coverage", coverage );
    optional( jo, was_loaded, "comfort", comfort, 0 );
    optional( jo, was_loaded, "floor_bedding_warmth", floor_bedding_warmth, 0 );
    optional( jo, was_loaded, "emissions", emissions );
    optional( jo, was_loaded, "bonus_fire_warmth_feet", bonus_fire_warmth_feet, 300 );
    optional( jo, was_loaded, "keg_capacity", keg_capacity, legacy_volume_reader, 0_ml );
    mandatory( jo, was_loaded, "required_str", move_str_req );
    optional( jo, was_loaded, "max_volume", max_volume, volume_reader(), DEFAULT_MAX_VOLUME_IN_SQUARE );
    optional( jo, was_loaded, "crafting_pseudo_item", crafting_pseudo_item, itype_id() );
    optional( jo, was_loaded, "deployed_item", deployed_item );
    load_symbol( jo );
    transparent = false;

    optional( jo, was_loaded, "light_emitted", light_emitted );

    // see the comment in ter_id::load for connect_group handling
    connect_group = TERCONN_NONE;
    for( auto &flag : jo.get_string_array( "flags" ) ) {
        set_flag( flag );
    }

    if( jo.has_member( "connects_to" ) ) {
        set_connects( jo.get_string( "connects_to" ) );
    }

    optional( jo, was_loaded, "open", open, string_id_reader<furn_t> {}, furn_str_id::NULL_ID() );
    optional( jo, was_loaded, "close", close, string_id_reader<furn_t> {}, furn_str_id::NULL_ID() );

    bash.load( jo, "bash", map_bash_info::furniture, "furniture " + id.str() );
    deconstruct.load( jo, "deconstruct", true, "furniture " + id.str() );

    if( jo.has_object( "workbench" ) ) {
        workbench = cata::make_value<furn_workbench_info>();
        workbench->load( jo, "workbench" );
    }
    if( jo.has_object( "plant_data" ) ) {
        plant = cata::make_value<plant_data>();
        plant->load( jo, "plant_data" );
    }
    if( jo.has_float( "surgery_skill_multiplier" ) ) {
        surgery_skill_multiplier = cata::make_value<float>( jo.get_float( "surgery_skill_multiplier" ) );
    }
}

void map_data_common_t::check() const
{
    if( examine_actor ) {
        examine_actor->finalize();
    }
    for( const string_id<harvest_list> &harvest : harvest_by_season ) {
        if( !harvest.is_null() && !can_examine() ) {
            debugmsg( "Harvest data defined without examine function for %s", name_ );
        }
    }
}

void furn_t::check() const
{
    map_data_common_t::check();
    check_bash_items( bash, id.str(), false );
    check_decon_items( deconstruct, id.str(), false );

    if( !open.is_valid() ) {
        debugmsg( "invalid furniture %s for opening %s", open.c_str(), id.c_str() );
    }
    if( !close.is_valid() ) {
        debugmsg( "invalid furniture %s for closing %s", close.c_str(), id.c_str() );
    }
    for( const emit_id &e : emissions ) {
        if( !e.is_valid() ) {
            debugmsg( "furn %s has invalid emission %s set", id.c_str(),
                      e.str().c_str() );
        }
    }
}

void check_furniture_and_terrain()
{
    terrain_data.check();
    furniture_data.check();
}
