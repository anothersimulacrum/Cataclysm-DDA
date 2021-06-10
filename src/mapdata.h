#pragma once
#ifndef CATA_SRC_MAPDATA_H
#define CATA_SRC_MAPDATA_H

#include <array>
#include <bitset>
#include <cstddef>
#include <iosfwd>
#include <set>
#include <string>
#include <vector>

#include "calendar.h"
#include "clone_ptr.h"
#include "color.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"

struct ter_t;

using ter_str_id = string_id<ter_t>;

class JsonObject;
class player;
struct iexamine_actor;
struct furn_t;
struct itype;
struct tripoint;

using iexamine_function = void ( * )( player &, const tripoint & );
using iexamine_function_ref = void( & )( player &, const tripoint & );

struct map_bash_info {
    int str_min;            // min str(*) required to bash
    int str_max;            // max str required: bash succeeds if str >= random # between str_min & str_max
    int str_min_blocked;    // same as above; alternate values for has_adjacent_furniture(...) == true
    int str_max_blocked;
    int str_min_supported;  // Alternative values for floor supported by something from below
    int str_max_supported;
    int explosive;          // Explosion on destruction
    int sound_vol;          // sound volume of breaking terrain/furniture
    int sound_fail_vol;     // sound volume on fail
    int collapse_radius;    // Radius of the tent supported by this tile
    int fd_bash_move_cost = 100; // cost to bash a field
    bool destroy_only;      // Only used for destroying, not normally bashable
    bool bash_below;        // This terrain is the roof of the tile below it, try to destroy that too
    item_group_id drop_group; // item group of items that are dropped when the object is bashed
    translation sound;      // sound made on success ('You hear a "smash!"')
    translation sound_fail; // sound  made on fail
    translation field_bash_msg_success; // message upon successfully bashing a field
    ter_str_id ter_set;    // terrain to set (REQUIRED for terrain))
    ter_str_id ter_set_bashed_from_above; // terrain to set if bashed from above (defaults to ter_set)
    furn_str_id furn_set;   // furniture to set (only used by furniture, not terrain)
    // ids used for the special handling of tents
    std::vector<furn_str_id> tent_centers;
    map_bash_info();
    enum map_object_type {
        furniture = 0,
        terrain,
        field
    };
    bool load( const JsonObject &jsobj, const std::string &member, map_object_type obj_type,
               const std::string &context );
};
struct map_deconstruct_info {
    // Only if true, the terrain/furniture can be deconstructed
    bool can_do;
    // This terrain provided a roof, we need to tear it down now
    bool deconstruct_above;
    // items you get when deconstructing.
    item_group_id drop_group;
    ter_str_id ter_set;    // terrain to set (REQUIRED for terrain))
    furn_str_id furn_set;    // furniture to set (only used by furniture, not terrain)
    map_deconstruct_info();
    bool load( const JsonObject &jsobj, const std::string &member, bool is_furniture,
               const std::string &context );
};
struct furn_workbench_info {
    // Base multiplier applied for crafting here
    float multiplier;
    // Mass/volume allowed before a crafting speed penalty is applied
    units::mass allowed_mass;
    units::volume allowed_volume;
    furn_workbench_info();
    bool load( const JsonObject &jsobj, const std::string &member );
};
struct plant_data {
    // What the furniture turns into when it grows or you plant seeds in it
    furn_str_id transform;
    // What the 'base' furniture of the plant is, before you plant in it, and what it turns into when eaten
    furn_str_id base;
    // At what percent speed of a normal plant this plant furniture grows at
    float growth_multiplier;
    // What percent of the normal harvest this crop gives
    float harvest_multiplier;
    plant_data();
    bool load( const JsonObject &jsobj, const std::string &member );
};

/*
 * List of known flags, used in both terrain.json and furniture.json.
 * TRANSPARENT - Players and monsters can see through/past it. Also sets ter_t.transparent
 * FLAT - Player can build and move furniture on
 * CONTAINER - Items on this square are hidden until looted by the player
 * PLACE_ITEM - Valid terrain for place_item() to put items on
 * DOOR - Can be opened (used for NPC pathfinding)
 * FLAMMABLE - Can be lit on fire
 * FLAMMABLE_HARD - Harder to light on fire, but still possible
 * DIGGABLE - Digging monsters, seeding monsters, digging with shovel, etc
 * LIQUID - Blocks movement, but isn't a wall (lava, water, etc)
 * SWIMMABLE - Player and monsters can swim through it
 * SHARP - May do minor damage to players/monsters passing through it
 * ROUGH - May hurt the player's feet
 * SEALED - Can't use 'e' to retrieve items, must smash open first
 * NOITEM - Items 'fall off' this space
 * NO_SIGHT - When on this tile sight is reduced to 1
 * NO_SCENT - Scent on this tile (and thus scent diffusing through it) is reduced to 0. This acts like a wall for scent
 * MOUNTABLE - Player can fire mounted weapons from here (e.g. M2 Browning)
 * DESTROY_ITEM - Items that land here are destroyed
 * GOES_DOWN - Can use '>' to go down a level
 * GOES_UP - Can use '<' to go up a level
 * CONSOLE - Used as a computer
 * ALARMED - Sets off an alarm if smashed
 * SUPPORTS_ROOF - Used as a boundary for roof construction
 * MINEABLE - Able to broken with the jackhammer/pickaxe, but does not necessarily support a roof
 * INDOORS - Has roof over it; blocks rain, sunlight, etc.
 * COLLAPSES - Has a roof that can collapse
 * FLAMMABLE_ASH - Burns to ash rather than rubble.
 * REDUCE_SCENT - Reduces scent even more, only works if also bashable
 * FIRE_CONTAINER - Stops fire from spreading (brazier, wood stove, etc)
 * SUPPRESS_SMOKE - Prevents smoke from fires, used by ventilated wood stoves etc
 * PLANT - A "furniture" that grows and fruits
 * LIQUIDCONT - Furniture that contains liquid, allows for contents to be accessed in some checks even if SEALED
 * OPENCLOSE_INSIDE - If it's a door (with an 'open' or 'close' field), it can only be opened or closed if you're inside.
 * PERMEABLE - Allows gases to flow through unimpeded.
 * RAMP - Higher z-levels can be accessed from this tile
 * EASY_DECONSTRUCT - Player can deconstruct this without tools
 * HIDE_PLACE - Creature on this tile can't be seen by other creature not standing on adjacent tiles
 * BLOCK_WIND - This tile will partially block wind
 * FLAT_SURF - Furniture or terrain or vehicle part with flat hard surface (ex. table, but not chair; tree stump, etc.).
 *
 * Currently only used for Fungal conversions
 * WALL - This terrain is an upright obstacle
 * THIN_OBSTACLE - This terrain is a thin obstacle, i.e. fence
 * ORGANIC - This furniture is partly organic
 * FLOWER - This furniture is a flower
 * SHRUB - This terrain is a shrub
 * TREE - This terrain is a tree
 * HARVESTED - This terrain has been harvested so it won't bear any fruit
 * YOUNG - This terrain is a young tree
 * FUNGUS - Fungal covered
 *
 * Furniture only:
 * BLOCKSDOOR - This will boost map terrain's resistance to bashing if str_*_blocked is set (see map_bash_info)
 * WORKBENCH1/WORKBENCH2/WORKBENCH3 - This is an adequate/good/great workbench for crafting.  Must be paired with a workbench iexamine.
 */

/*
 * Note; All flags are defined as strings dynamically in data/json/terrain.json and furniture.json. The list above
 * represent the common builtins. The enum below is an alternative means of fast-access, for those flags that are checked
 * so much that strings produce a significant performance penalty. The following are equivalent:
 *  m->has_flag("FLAMMABLE");     //
 *  m->has_flag(TFLAG_FLAMMABLE); // ~ 20 x faster than the above, ( 2.5 x faster if the above uses static const std::string str_flammable("FLAMMABLE");
 * To add a new ter_bitflag, add below and add to ter_bitflags_map in mapdata.cpp
 * Order does not matter.
 */
enum ter_bitflags : int {
    TFLAG_TRANSPARENT,
    TFLAG_FLAMMABLE,
    TFLAG_REDUCE_SCENT,
    TFLAG_SWIMMABLE,
    TFLAG_SUPPORTS_ROOF,
    TFLAG_MINEABLE,
    TFLAG_NOITEM,
    TFLAG_NO_SIGHT,
    TFLAG_NO_SCENT,
    TFLAG_SEALED,
    TFLAG_ALLOW_FIELD_EFFECT,
    TFLAG_LIQUID,
    TFLAG_COLLAPSES,
    TFLAG_FLAMMABLE_ASH,
    TFLAG_DESTROY_ITEM,
    TFLAG_INDOORS,
    TFLAG_LIQUIDCONT,
    TFLAG_FIRE_CONTAINER,
    TFLAG_FLAMMABLE_HARD,
    TFLAG_SUPPRESS_SMOKE,
    TFLAG_SHARP,
    TFLAG_DIGGABLE,
    TFLAG_ROUGH,
    TFLAG_UNSTABLE,
    TFLAG_WALL,
    TFLAG_DEEP_WATER,
    TFLAG_SHALLOW_WATER,
    TFLAG_NO_SHOOT,
    TFLAG_CURRENT,
    TFLAG_HARVESTED,
    TFLAG_PERMEABLE,
    TFLAG_AUTO_WALL_SYMBOL,
    TFLAG_CONNECT_TO_WALL,
    TFLAG_CLIMBABLE,
    TFLAG_GOES_DOWN,
    TFLAG_GOES_UP,
    TFLAG_NO_FLOOR,
    TFLAG_SEEN_FROM_ABOVE,
    TFLAG_RAMP_DOWN,
    TFLAG_RAMP_UP,
    TFLAG_RAMP,
    TFLAG_HIDE_PLACE,
    TFLAG_BLOCK_WIND,
    TFLAG_FLAT,
    TFLAG_RAIL,
    TFLAG_THIN_OBSTACLE,
    TFLAG_SMALL_PASSAGE,
    TFLAG_Z_TRANSPARENT,
    TFLAG_SUN_ROOF_ABOVE,
    TFLAG_FUNGUS,

    NUM_TERFLAGS
};

/*
 * Terrain groups which affect whether the terrain connects visually.
 * Groups are also defined in ter_connects_map() in mapdata.cpp which matches group to JSON string.
 */
enum ter_connects : int {
    TERCONN_NONE,
    TERCONN_WALL,
    TERCONN_CHAINFENCE,
    TERCONN_WOODFENCE,
    TERCONN_RAILING,
    TERCONN_POOLWATER,
    TERCONN_WATER,
    TERCONN_PAVEMENT,
    TERCONN_RAIL,
    TERCONN_COUNTER,
    TERCONN_CANVAS_WALL,
};

void init_mapdata();

struct map_data_common_t {
        map_bash_info        bash;
        map_deconstruct_info deconstruct;

    public:
        virtual ~map_data_common_t() = default;

    protected:
        friend furn_t null_furniture_t();
        friend ter_t null_terrain_t();
        // The (untranslated) plaintext name of the terrain type the user would see (i.e. dirt)
        translation name_;

        // Hardcoded examination function
        iexamine_function examine_func; // What happens when the terrain/furniture is examined
        // Data-driven examine actor
        cata::clone_ptr<iexamine_actor> examine_actor;

    private:
        std::set<std::string> flags;    // string flags which possibly refer to what's documented above.
        std::bitset<NUM_TERFLAGS> bitflags; // bitfield of -certain- string flags which are heavily checked

    public:
        ter_str_id curtain_transform;

        bool has_curtains() const {
            return !( curtain_transform.is_empty() || curtain_transform.is_null() );
        }

    public:
        std::string name() const;

        /*
        * The symbol drawn on the screen for the terrain. Please note that
        * there are extensive rules as to which possible object/field/entity in
        * a single square gets drawn and that some symbols are "reserved" such
        * as * and % to do programmatic behavior.
        */
        std::array<int, NUM_SEASONS> symbol_;

        bool can_examine() const;
        bool has_examine( iexamine_function_ref func ) const;
        bool has_examine( const std::string &action ) const;
        void set_examine( iexamine_function_ref func );
        void examine( player &, const tripoint & ) const;

        int light_emitted = 0;
        // The amount of movement points required to pass this terrain by default.
        int movecost = 0;
        int heat_radiation = 0;
        // The coverage percentage of a furniture piece of terrain. <30 won't cover from sight.
        int coverage = 0;
        // Maximal volume of items that can be stored in/on this furniture
        units::volume max_volume = 1000_liter;

        translation description;

        // The color the sym will draw in on the GUI.
        std::array<nc_color, NUM_SEASONS> color_;
        void load_symbol( const JsonObject &jo );

        std::string looks_like;

        /**
         * When will this terrain/furniture get harvested and what will drop?
         * Note: This excludes items that take extra tools to harvest.
         */
        std::array<harvest_id, NUM_SEASONS> harvest_by_season = {{
                harvest_id::NULL_ID(), harvest_id::NULL_ID(), harvest_id::NULL_ID(), harvest_id::NULL_ID()
            }
        };

        bool transparent = false;

        const std::set<std::string> &get_flags() const {
            return flags;
        }

        bool has_flag( const std::string &flag ) const {
            return flags.count( flag ) > 0;
        }

        bool has_flag( const ter_bitflags flag ) const {
            return bitflags.test( flag );
        }

        void set_flag( const std::string &flag );

        int connect_group = 0;

        void set_connects( const std::string &connect_group_string );

        bool connects( int &ret ) const;

        bool connects_to( int test_connect_group ) const {
            return connect_group != TERCONN_NONE && connect_group == test_connect_group;
        }

        int symbol() const;
        nc_color color() const;

        const harvest_id &get_harvest() const;
        /**
         * Returns a set of names of the items that would be dropped.
         * Used for NPC whitelist checking.
         */
        const std::set<std::string> &get_harvest_names() const;

        std::string extended_description() const;

        bool was_loaded = false;

        bool is_flammable() const {
            return has_flag( TFLAG_FLAMMABLE ) || has_flag( TFLAG_FLAMMABLE_ASH ) ||
                   has_flag( TFLAG_FLAMMABLE_HARD );
        }

        virtual void load( const JsonObject &jo, const std::string & );
        virtual void check() const;
};

/*
* Struct ter_t:
* Short for terrain type. This struct defines all of the metadata for a given terrain id (an enum below).
*/
struct ter_t : map_data_common_t {
    ter_str_id id;    // The terrain's ID. Must be set, must be unique.
    ter_str_id open;  // Open action: transform into terrain with matching id
    ter_str_id close; // Close action: transform into terrain with matching id

    std::string trap_id_str;     // String storing the id string of the trap.
    ter_str_id transforms_into; // Transform into what terrain?
    ter_str_id roof;            // What will be the floor above this terrain

    trap_id trap; // The id of the trap located at this terrain. Limit one trap per tile currently.

    std::set<emit_id> emissions;
    std::set<itype_id> allowed_template_id;

    ter_t();

    static size_t count();

    void load( const JsonObject &jo, const std::string &src ) override;
    void check() const override;
};

void set_ter_ids();
void set_furn_ids();
void reset_furn_ter();

/*
 * The terrain list contains the master list of  information and metadata for a given type of terrain.
 */

struct furn_t : map_data_common_t {
    furn_str_id id;
    furn_str_id open;  // Open action: transform into furniture with matching id
    furn_str_id close; // Close action: transform into furniture with matching id
    itype_id crafting_pseudo_item;
    units::volume keg_capacity = 0_ml;
    int comfort = 0;
    int floor_bedding_warmth = 0;
    /** Emissions of furniture */
    std::set<emit_id> emissions;

    int bonus_fire_warmth_feet = 300;
    itype_id deployed_item; // item id string used to create furniture

    int move_str_req = 0; //The amount of strength required to move through this furniture easily.

    cata::value_ptr<furn_workbench_info> workbench;

    cata::value_ptr<plant_data> plant;

    cata::value_ptr<float> surgery_skill_multiplier;

    // May return NULL
    const itype *crafting_pseudo_item_type() const;
    // May return NULL
    const itype *crafting_ammo_item_type() const;

    furn_t();

    static size_t count();

    bool is_movable() const;

    void load( const JsonObject &jo, const std::string &src ) override;
    void check() const override;
};

void load_furniture( const JsonObject &jo, const std::string &src );
void load_terrain( const JsonObject &jo, const std::string &src );

void verify_furniture();
void verify_terrain();

/*
runtime index: ter_id
ter_id refers to a position in the terlist[] where the ter_t struct is stored. These global
ints are a drop-in replacement to the old enum, however they are -not- required (save for areas in
the code that can use the performance boost and refer to core terrain types), and they are -not-
provided for terrains added by mods. A string equivalent is always present, i.e.;
t_basalt
"t_basalt"
*/
// NOLINTNEXTLINE(cata-static-int_id-constants)
extern ter_id t_null;
extern const ter_str_id t_str_null;
extern const ter_str_id t_hole; // Real nothingness; makes you fall a z-level
// Ground
extern const ter_str_id t_dirt;
extern const ter_str_id t_sand;
extern const ter_str_id t_clay;
extern const ter_str_id t_dirtmound;
extern const ter_str_id t_pit_shallow;
extern const ter_str_id t_pit;
extern const ter_str_id t_grave;
extern const ter_str_id t_grave_new;
extern const ter_str_id t_pit_corpsed;
extern const ter_str_id t_pit_covered;
extern const ter_str_id t_pit_spiked;
extern const ter_str_id t_pit_spiked_covered;
extern const ter_str_id t_pit_glass;
extern const ter_str_id t_pit_glass_covered;
extern const ter_str_id t_rock_floor;
extern const ter_str_id t_grass;
extern const ter_str_id t_grass_long;
extern const ter_str_id t_grass_tall;
extern const ter_str_id t_grass_golf;
extern const ter_str_id t_grass_dead;
extern const ter_str_id t_grass_white;
extern const ter_str_id t_moss;
extern const ter_str_id t_metal_floor;
extern const ter_str_id t_pavement;
extern const ter_str_id t_pavement_y;
extern const ter_str_id t_sidewalk;
extern const ter_str_id t_concrete;
extern const ter_str_id t_zebra;
extern const ter_str_id t_thconc_floor;
extern const ter_str_id t_thconc_floor_olight;
extern const ter_str_id t_strconc_floor;
extern const ter_str_id t_floor;
extern const ter_str_id t_floor_waxed;
extern const ter_str_id t_dirtfloor; //Dirt floor(Has roof)
extern const ter_str_id t_carpet_red;
extern const ter_str_id t_carpet_yellow;
extern const ter_str_id t_carpet_purple;
extern const ter_str_id t_carpet_green;
extern const ter_str_id t_grate;
extern const ter_str_id t_slime;
extern const ter_str_id t_bridge;
extern const ter_str_id t_covered_well;
// Lighting related
extern const ter_str_id t_utility_light;
// Walls
extern const ter_str_id t_wall_log_half;
extern const ter_str_id t_wall_log;
extern const ter_str_id t_wall_log_chipped;
extern const ter_str_id t_wall_log_broken;
extern const ter_str_id t_palisade;
extern const ter_str_id t_palisade_gate;
extern const ter_str_id t_palisade_gate_o;
extern const ter_str_id t_wall_half;
extern const ter_str_id t_wall_wood;
extern const ter_str_id t_wall_wood_chipped;
extern const ter_str_id t_wall_wood_broken;
extern const ter_str_id t_wall;
extern const ter_str_id t_concrete_wall;
extern const ter_str_id t_brick_wall;
extern const ter_str_id t_wall_metal;
extern const ter_str_id t_scrap_wall;
extern const ter_str_id t_scrap_wall_halfway;
extern const ter_str_id t_wall_glass;
extern const ter_str_id t_wall_glass_alarm;
extern const ter_str_id t_reinforced_glass;
extern const ter_str_id t_reinforced_glass_shutter;
extern const ter_str_id t_reinforced_glass_shutter_open;
extern const ter_str_id t_laminated_glass;
extern const ter_str_id t_ballistic_glass;
extern const ter_str_id t_reinforced_door_glass_o;
extern const ter_str_id t_reinforced_door_glass_c;
extern const ter_str_id t_bars;
extern const ter_str_id t_reb_cage;
extern const ter_str_id t_door_c;
extern const ter_str_id t_door_c_peep;
extern const ter_str_id t_door_b;
extern const ter_str_id t_door_b_peep;
extern const ter_str_id t_door_o;
extern const ter_str_id t_door_o_peep;
extern const ter_str_id t_door_locked_interior;
extern const ter_str_id t_door_locked;
extern const ter_str_id t_door_locked_peep;
extern const ter_str_id t_door_locked_alarm;
extern const ter_str_id t_door_frame;
extern const ter_str_id t_chaingate_l;
extern const ter_str_id t_fencegate_c;
extern const ter_str_id t_fencegate_o;
extern const ter_str_id t_chaingate_c;
extern const ter_str_id t_chaingate_o;
extern const ter_str_id t_retractable_gate_l;
extern const ter_str_id t_retractable_gate_c;
extern const ter_str_id t_retractable_gate_o;
extern const ter_str_id t_door_boarded;
extern const ter_str_id t_door_boarded_damaged;
extern const ter_str_id t_door_boarded_peep;
extern const ter_str_id t_rdoor_boarded;
extern const ter_str_id t_rdoor_boarded_damaged;
extern const ter_str_id t_door_boarded_damaged_peep;
extern const ter_str_id t_door_metal_c;
extern const ter_str_id t_door_metal_o;
extern const ter_str_id t_door_metal_locked;
extern const ter_str_id t_door_metal_pickable;
extern const ter_str_id t_door_bar_c;
extern const ter_str_id t_door_bar_o;
extern const ter_str_id t_door_bar_locked;
extern const ter_str_id t_door_glass_c;
extern const ter_str_id t_door_glass_o;
extern const ter_str_id t_door_glass_frosted_c;
extern const ter_str_id t_door_glass_frosted_o;
extern const ter_str_id t_portcullis;
extern const ter_str_id t_recycler;
extern const ter_str_id t_window;
extern const ter_str_id t_window_taped;
extern const ter_str_id t_window_domestic;
extern const ter_str_id t_window_domestic_taped;
extern const ter_str_id t_window_open;
extern const ter_str_id t_curtains;
extern const ter_str_id t_window_bars_curtains;
extern const ter_str_id t_window_bars_domestic;
extern const ter_str_id t_window_alarm;
extern const ter_str_id t_window_alarm_taped;
extern const ter_str_id t_window_empty;
extern const ter_str_id t_window_frame;
extern const ter_str_id t_window_boarded;
extern const ter_str_id t_window_boarded_noglass;
extern const ter_str_id t_window_bars_alarm;
extern const ter_str_id t_window_bars;
extern const ter_str_id t_metal_grate_window;
extern const ter_str_id t_metal_grate_window_with_curtain;
extern const ter_str_id t_metal_grate_window_with_curtain_open;
extern const ter_str_id t_metal_grate_window_noglass;
extern const ter_str_id t_metal_grate_window_with_curtain_noglass;
extern const ter_str_id t_metal_grate_window_with_curtain_open_noglass;
extern const ter_str_id t_window_stained_green;
extern const ter_str_id t_window_stained_red;
extern const ter_str_id t_window_stained_blue;
extern const ter_str_id t_window_no_curtains;
extern const ter_str_id t_window_no_curtains_open;
extern const ter_str_id t_window_no_curtains_taped;
extern const ter_str_id t_rock;
extern const ter_str_id t_fault;
extern const ter_str_id t_paper;
extern const ter_str_id t_rock_wall;
extern const ter_str_id t_rock_wall_half;
// Tree
extern const ter_str_id t_tree;
extern const ter_str_id t_tree_young;
extern const ter_str_id t_tree_apple;
extern const ter_str_id t_tree_apple_harvested;
extern const ter_str_id t_tree_coffee;
extern const ter_str_id t_tree_coffee_harvested;
extern const ter_str_id t_tree_pear;
extern const ter_str_id t_tree_pear_harvested;
extern const ter_str_id t_tree_cherry;
extern const ter_str_id t_tree_cherry_harvested;
extern const ter_str_id t_tree_peach;
extern const ter_str_id t_tree_peach_harvested;
extern const ter_str_id t_tree_apricot;
extern const ter_str_id t_tree_apricot_harvested;
extern const ter_str_id t_tree_plum;
extern const ter_str_id t_tree_plum_harvested;
extern const ter_str_id t_tree_pine;
extern const ter_str_id t_tree_blackjack;
extern const ter_str_id t_tree_birch;
extern const ter_str_id t_tree_birch_harvested;
extern const ter_str_id t_tree_willow;
extern const ter_str_id t_tree_willow_harvested;
extern const ter_str_id t_tree_maple;
extern const ter_str_id t_tree_maple_tapped;
extern const ter_str_id t_tree_deadpine;
extern const ter_str_id t_tree_hickory;
extern const ter_str_id t_tree_hickory_dead;
extern const ter_str_id t_tree_hickory_harvested;
extern const ter_str_id t_underbrush;
extern const ter_str_id t_shrub;
extern const ter_str_id t_shrub_blueberry;
extern const ter_str_id t_shrub_strawberry;
extern const ter_str_id t_trunk;
extern const ter_str_id t_stump;
extern const ter_str_id t_root_wall;
extern const ter_str_id t_wax;
extern const ter_str_id t_floor_wax;
extern const ter_str_id t_fence;
extern const ter_str_id t_chainfence;
extern const ter_str_id t_chainfence_posts;
extern const ter_str_id t_fence_post;
extern const ter_str_id t_fence_wire;
extern const ter_str_id t_fence_barbed;
extern const ter_str_id t_fence_rope;
extern const ter_str_id t_railing;
// Nether
extern const ter_str_id t_marloss;
extern const ter_str_id t_fungus_floor_in;
extern const ter_str_id t_fungus_floor_sup;
extern const ter_str_id t_fungus_floor_out;
extern const ter_str_id t_fungus_wall;
extern const ter_str_id t_fungus_mound;
extern const ter_str_id t_fungus;
extern const ter_str_id t_shrub_fungal;
extern const ter_str_id t_tree_fungal;
extern const ter_str_id t_tree_fungal_young;
extern const ter_str_id t_marloss_tree;
// Water, lava, etc.
extern const ter_str_id t_water_moving_dp;
extern const ter_str_id t_water_moving_sh;
extern const ter_str_id t_water_sh;
extern const ter_str_id t_swater_sh;
extern const ter_str_id t_water_dp;
extern const ter_str_id t_swater_dp;
extern const ter_str_id t_water_pool;
extern const ter_str_id t_sewage;
extern const ter_str_id t_lava;
// More embellishments than you can shake a stick at.
extern const ter_str_id t_sandbox;
extern const ter_str_id t_slide;
extern const ter_str_id t_monkey_bars;
extern const ter_str_id t_backboard;
extern const ter_str_id t_gas_pump;
extern const ter_str_id t_gas_pump_smashed;
extern const ter_str_id t_diesel_pump;
extern const ter_str_id t_diesel_pump_smashed;
extern const ter_str_id t_atm;
extern const ter_str_id t_generator_broken;
extern const ter_str_id t_missile;
extern const ter_str_id t_missile_exploded;
extern const ter_str_id t_radio_tower;
extern const ter_str_id t_radio_controls;
extern const ter_str_id t_console_broken;
extern const ter_str_id t_console;
extern const ter_str_id t_gates_mech_control;
extern const ter_str_id t_gates_control_concrete;
extern const ter_str_id t_gates_control_brick;
extern const ter_str_id t_barndoor;
extern const ter_str_id t_palisade_pulley;
extern const ter_str_id t_gates_control_metal;
extern const ter_str_id t_sewage_pipe;
extern const ter_str_id t_sewage_pump;
extern const ter_str_id t_centrifuge;
extern const ter_str_id t_column;
extern const ter_str_id t_vat;
extern const ter_str_id t_rootcellar;
extern const ter_str_id t_cvdbody;
extern const ter_str_id t_cvdmachine;
extern const ter_str_id t_water_pump;
extern const ter_str_id t_conveyor;
extern const ter_str_id t_machinery_light;
extern const ter_str_id t_machinery_heavy;
extern const ter_str_id t_machinery_old;
extern const ter_str_id t_machinery_electronic;
extern const ter_str_id t_improvised_shelter;
// Staircases etc.
extern const ter_str_id t_stairs_down;
extern const ter_str_id t_stairs_up;
extern const ter_str_id t_manhole;
extern const ter_str_id t_ladder_up;
extern const ter_str_id t_ladder_down;
extern const ter_str_id t_slope_down;
extern const ter_str_id t_slope_up;
extern const ter_str_id t_rope_up;
extern const ter_str_id t_manhole_cover;
// Special
extern const ter_str_id t_card_science;
extern const ter_str_id t_card_military;
extern const ter_str_id t_card_industrial;
extern const ter_str_id t_card_reader_broken;
extern const ter_str_id t_slot_machine;
extern const ter_str_id t_elevator_control;
extern const ter_str_id t_elevator_control_off;
extern const ter_str_id t_elevator;
extern const ter_str_id t_pedestal_wyrm;
extern const ter_str_id t_pedestal_temple;
// Temple tiles
extern const ter_str_id t_rock_red;
extern const ter_str_id t_rock_green;
extern const ter_str_id t_rock_blue;
extern const ter_str_id t_floor_red;
extern const ter_str_id t_floor_green;
extern const ter_str_id t_floor_blue;
extern const ter_str_id t_switch_rg;
extern const ter_str_id t_switch_gb;
extern const ter_str_id t_switch_rb;
extern const ter_str_id t_switch_even;
extern const ter_str_id t_rdoor_c;
extern const ter_str_id t_rdoor_b;
extern const ter_str_id t_rdoor_o;
extern const ter_str_id t_mdoor_frame;
extern const ter_str_id t_window_reinforced;
extern const ter_str_id t_window_reinforced_noglass;
extern const ter_str_id t_window_enhanced;
extern const ter_str_id t_window_enhanced_noglass;
extern const ter_str_id t_open_air;
extern const ter_str_id t_plut_generator;
extern const ter_str_id t_pavement_bg_dp;
extern const ter_str_id t_pavement_y_bg_dp;
extern const ter_str_id t_sidewalk_bg_dp;
extern const ter_str_id t_guardrail_bg_dp;
extern const ter_str_id t_linoleum_white;
extern const ter_str_id t_linoleum_gray;
extern const ter_str_id t_rad_platform;
// Railroad and subway
extern const ter_str_id t_railroad_rubble;
extern const ter_str_id t_buffer_stop;
extern const ter_str_id t_railroad_crossing_signal;
extern const ter_str_id t_crossbuck_wood;
extern const ter_str_id t_crossbuck_metal;
extern const ter_str_id t_railroad_tie;
extern const ter_str_id t_railroad_tie_h;
extern const ter_str_id t_railroad_tie_v;
extern const ter_str_id t_railroad_tie_d;
extern const ter_str_id t_railroad_track;
extern const ter_str_id t_railroad_track_h;
extern const ter_str_id t_railroad_track_v;
extern const ter_str_id t_railroad_track_d;
extern const ter_str_id t_railroad_track_d1;
extern const ter_str_id t_railroad_track_d2;
extern const ter_str_id t_railroad_track_on_tie;
extern const ter_str_id t_railroad_track_h_on_tie;
extern const ter_str_id t_railroad_track_v_on_tie;
extern const ter_str_id t_railroad_track_d_on_tie;

/*
runtime index: furn_id
furn_id refers to a position in the furnlist[] where the furn_t struct is stored. See note
about ter_id above.
*/
// NOLINTNEXTLINE(cata-static-int_id-constants)
extern furn_id f_null;
extern const furn_str_id f_str_null;
extern const furn_str_id f_hay;
extern const furn_str_id f_cattails;
extern const furn_str_id f_lotus;
extern const furn_str_id f_lilypad;
extern const furn_str_id f_rubble;
extern const furn_str_id f_rubble_rock;
extern const furn_str_id f_wreckage;
extern const furn_str_id f_ash;
extern const furn_str_id f_barricade_road;
extern const furn_str_id f_sandbag_half;
extern const furn_str_id f_sandbag_wall;
extern const furn_str_id f_bulletin;
extern const furn_str_id f_indoor_plant;
extern const furn_str_id f_bed;
extern const furn_str_id f_toilet;
extern const furn_str_id f_makeshift_bed;
extern const furn_str_id f_straw_bed;
extern const furn_str_id f_sink;
extern const furn_str_id f_oven;
extern const furn_str_id f_woodstove;
extern const furn_str_id f_fireplace;
extern const furn_str_id f_bathtub;
extern const furn_str_id f_chair;
extern const furn_str_id f_armchair;
extern const furn_str_id f_sofa;
extern const furn_str_id f_cupboard;
extern const furn_str_id f_trashcan;
extern const furn_str_id f_desk;
extern const furn_str_id f_exercise;
extern const furn_str_id f_bench;
extern const furn_str_id f_table;
extern const furn_str_id f_pool_table;
extern const furn_str_id f_counter;
extern const furn_str_id f_fridge;
extern const furn_str_id f_glass_fridge;
extern const furn_str_id f_dresser;
extern const furn_str_id f_locker;
extern const furn_str_id f_rack;
extern const furn_str_id f_bookcase;
extern const furn_str_id f_washer;
extern const furn_str_id f_dryer;
extern const furn_str_id f_vending_c;
extern const furn_str_id f_vending_o;
extern const furn_str_id f_dumpster;
extern const furn_str_id f_dive_block;
extern const furn_str_id f_crate_c;
extern const furn_str_id f_crate_o;
extern const furn_str_id f_coffin_c;
extern const furn_str_id f_coffin_o;
extern const furn_str_id f_large_canvas_wall;
extern const furn_str_id f_canvas_wall;
extern const furn_str_id f_canvas_door;
extern const furn_str_id f_canvas_door_o;
extern const furn_str_id f_groundsheet;
extern const furn_str_id f_fema_groundsheet;
extern const furn_str_id f_large_groundsheet;
extern const furn_str_id f_large_canvas_door;
extern const furn_str_id f_large_canvas_door_o;
extern const furn_str_id f_center_groundsheet;
extern const furn_str_id f_skin_wall;
extern const furn_str_id f_skin_door;
extern const furn_str_id f_skin_door_o;
extern const furn_str_id f_skin_groundsheet;
extern const furn_str_id f_mutpoppy;
extern const furn_str_id f_flower_fungal;
extern const furn_str_id f_fungal_mass;
extern const furn_str_id f_fungal_clump;
extern const furn_str_id f_safe_c;
extern const furn_str_id f_safe_l;
extern const furn_str_id f_safe_o;
extern const furn_str_id f_plant_seed;
extern const furn_str_id f_plant_seedling;
extern const furn_str_id f_plant_mature;
extern const furn_str_id f_plant_harvest;
extern const furn_str_id f_fvat_empty;
extern const furn_str_id f_fvat_full;
extern const furn_str_id f_wood_keg;
extern const furn_str_id f_standing_tank;
extern const furn_str_id f_egg_sackbw;
extern const furn_str_id f_egg_sackcs;
extern const furn_str_id f_egg_sackws;
extern const furn_str_id f_egg_sacke;
extern const furn_str_id f_flower_marloss;
extern const furn_str_id f_tatami;
extern const furn_str_id f_kiln_empty;
extern const furn_str_id f_kiln_full;
extern const furn_str_id f_kiln_metal_empty;
extern const furn_str_id f_kiln_metal_full;
extern const furn_str_id f_arcfurnace_empty;
extern const furn_str_id f_arcfurnace_full;
extern const furn_str_id f_smoking_rack;
extern const furn_str_id f_smoking_rack_active;
extern const furn_str_id f_metal_smoking_rack;
extern const furn_str_id f_metal_smoking_rack_active;
extern const furn_str_id f_water_mill;
extern const furn_str_id f_water_mill_active;
extern const furn_str_id f_wind_mill;
extern const furn_str_id f_wind_mill_active;
extern const furn_str_id f_robotic_arm;
extern const furn_str_id f_vending_reinforced;
extern const furn_str_id f_brazier;
extern const furn_str_id f_firering;
extern const furn_str_id f_tourist_table;
extern const furn_str_id f_camp_chair;
extern const furn_str_id f_sign;
extern const furn_str_id f_gunsafe_ml;
extern const furn_str_id f_gunsafe_mj;
extern const furn_str_id f_gun_safe_el;
extern const furn_str_id f_street_light;
extern const furn_str_id f_traffic_light;
extern const furn_str_id f_console;
extern const furn_str_id f_console_broken;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// These are on their way OUT and only used in certain switch statements until they are rewritten.

// consistency checking of terlist & furnlist.
void check_furniture_and_terrain();

void finalize_furniture_and_terrain();

#endif // CATA_SRC_MAPDATA_H
