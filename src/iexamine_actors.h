#pragma once
#ifndef CATA_SRC_IEXAMINE_ACTORS_H
#define CATA_SRC_IEXAMINE_ACTORS_H

#include "iexamine.h"

#include <map>

#include "mapdata.h"
#include "ui.h"
#include "units.h"

class Character;

class cardreader_examine_actor : public iexamine_actor
{
    private:
        std::vector<flag_id> allowed_flags;
        bool consume = true;
        bool allow_hacking = true;
        bool despawn_monsters = true;

        // Option 1: walk the map, do some stuff
        int radius = 3;
        std::map<ter_str_id, ter_str_id> terrain_changes;
        std::map<furn_str_id, furn_str_id> furn_changes;
        // Option 2: Regenerate entire current overmap tile
        std::string mapgen_id;

        bool map_regen = false;

        bool query = true;
        std::string query_msg;

        std::string success_msg;
        std::string redundant_msg;

        void consume_card( player &guy ) const;
        bool apply( const tripoint &examp ) const;

    public:
        explicit cardreader_examine_actor( const std::string &type = "cardreader" )
            : iexamine_actor( type ) {}

        void load( const JsonObject &jo ) override;
        void call( player *guyp, const tripoint &examp ) const override;
        void finalize() const override;

        std::unique_ptr<iexamine_actor> clone() const override;
};

class crafter_examine_actor : public iexamine_actor
{
    private:
        bool active = false;
        bool portable = false;

        bool can_disable = false;

        furn_id f_transform = f_null;
        ter_id t_transform = t_null;

        itype_id fake_item = itype_id::NULL_ID();

        itype_id fuel = itype_id::NULL_ID();

        int min_fuel = 0;
        int fuel_per_liter = 0;

        units::volume max_processed_volume = 0_liter;

        flag_id processing_flag;

        enum class options : int {
            inspect,
            start,
            add,
            remove,
            reload,
            remove_fuel,
            disable,
            disassemble,
            invalid = UILIST_CANCEL
        };

        translation name;
        translation disable_name;
        translation disable_desc;
        translation start_msg;
        translation start_desc;
        translation start_msg_no_items;
        translation start_msg_no_fuel;
        translation add_full_msg;
        translation add_space_msg;
        translation add_desc;
        translation remove_items_msg;
        translation remove_no_msg;
        translation reload_items_msg;
        translation reload_no_msg;
        translation reload_desc;
        translation disassemble_msg;
        translation remove_fuel_msg;
        translation active_str;
        translation no_load_active_msg;
        translation load_query_msg;

        units::volume free_volume() const;

        void process( const tripoint &examp ) const;
        void show_options( player &guy, const tripoint &examp ) const;
        int query_options( player &guy, const tripoint &examp ) const;

        void transform( const tripoint &examp ) const;
        void disassemble( const tripoint &examp ) const;
        void display_info( player &guy, const tripoint &examp ) const;
        void activate( player &guy, const tripoint &examp ) const;
        void load_items( player &guy, const tripoint &examp ) const;
        void remove_items( player &guy, const tripoint &examp ) const;
        void insert_fuel( player &guy, const tripoint &examp ) const;
        void remove_fuel( player &guy, const tripoint &examp ) const;

    public:
        crafter_examine_actor( const std::string &type = "crafter" ) : iexamine_actor( type ) {}

        void load( const JsonObject &jo ) override;
        void call( player *guy, const tripoint &examp ) const override;
        void finalize() const override;

        std::unique_ptr<iexamine_actor> clone() const override;
};

#endif // CATA_SRC_IEXAMINE_ACTORS_H
