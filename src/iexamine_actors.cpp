#include "iexamine_actors.h"

#include "game.h"
#include "generic_factory.h"
#include "itype.h"
#include "inventory.h"
#include "map.h"
#include "mapgen_functions.h"
#include "map_iterator.h"
#include "messages.h"
#include "output.h"
#include "player.h"
#include "units_utility.h"

////////////////// Cardreader /////////////////////////////

void cardreader_examine_actor::consume_card( player &guy ) const
{
    std::vector<itype_id> cards;
    for( const flag_id &flag : allowed_flags ) {
        for( const item *it : guy.all_items_with_flag( flag ) ) {
            itype_id card_type = it->typeId();
            if( std::find( cards.begin(), cards.end(), card_type ) == cards.end() ) {
                cards.push_back( card_type );
            }
        }
    }
    if( cards.size() > 1 ) {
        uilist query;
        query.text = _( "Use which item?" );
        for( size_t i = 0; i < cards.size(); ++i ) {
            query.entries.emplace_back( static_cast<int>( i ), true, -1, cards[i]->nname( 1 ) );
        }
        query.query();
        while( query.ret == UILIST_CANCEL ) {
            query.query();
        }
        guy.use_amount( cards[query.ret], 1 );
        return;
    }
    guy.use_amount( cards[0], 1 );
}

bool cardreader_examine_actor::apply( const tripoint &examp ) const
{
    bool open = true;

    map &here = get_map();
    if( map_regen ) {
        tripoint_abs_omt omt_pos( ms_to_omt_copy( here.getabs( examp ) ) );
        if( !run_mapgen_update_func( mapgen_id, omt_pos, nullptr, false ) ) {
            debugmsg( "Failed to apply magen function %s", mapgen_id );
        }
        here.set_seen_cache_dirty( examp );
        here.set_transparency_cache_dirty( examp.z );
    } else {
        open = false;
        const tripoint_range<tripoint> points = here.points_in_radius( examp, radius );
        for( const tripoint &tmp : points ) {
            const auto ter_iter = terrain_changes.find( here.ter( tmp ).id() );
            const auto furn_iter = furn_changes.find( here.furn( tmp ).id() );
            if( ter_iter != terrain_changes.end() ) {
                here.ter_set( tmp, ter_iter->second );
                open = true;
            }
            if( furn_iter != furn_changes.end() ) {
                here.furn_set( tmp, furn_iter->second );
                open = true;
            }
        }
    }

    return open;
}

/**
 * Use id/hack reader. Using an id despawns turrets.
 */
void cardreader_examine_actor::call( player *guyp, const tripoint &examp ) const
{
    if( guyp == nullptr ) {
        debugmsg( "Called cardreader_examine_actor with no player!" );
        return;
    }
    bool open = false;
    map &here = get_map();
    player &guy = *guyp;

    bool has_item = false;
    for( const flag_id &flag : allowed_flags ) {
        if( guy.has_item_with_flag( flag ) ) {
            has_item = true;
            break;
        }
    }


    if( has_item && query_yn( _( query_msg ) ) ) {
        guy.mod_moves( -to_moves<int>( 1_seconds ) );
        open = apply( examp );
        for( monster &critter : g->all_monsters() ) {
            if( !despawn_monsters ) {
                break;
            }
            // Check 1) same overmap coords, 2) turret, 3) hostile
            if( ms_to_omt_copy( here.getabs( critter.pos() ) ) == ms_to_omt_copy( here.getabs( examp ) ) &&
                critter.has_flag( MF_ID_CARD_DESPAWN ) &&
                critter.attitude_to( guy ) == Creature::Attitude::HOSTILE ) {
                g->remove_zombie( critter );
            }
        }
        if( open ) {
            add_msg( _( success_msg ) );
            consume_card( guy );
        } else {
            add_msg( _( redundant_msg ) );
        }
    } else if( allow_hacking && query_yn( _( "Attempt to hack this card-reader?" ) ) ) {
        iexamine::try_start_hacking( guy, examp );
    }
}

void cardreader_examine_actor::load( const JsonObject &jo )
{
    mandatory( jo, false, "flags", allowed_flags );
    optional( jo, false, "consume_card", consume, true );
    optional( jo, false, "allow_hacking", allow_hacking, true );
    optional( jo, false, "despawn_monsters", despawn_monsters, true );
    if( jo.has_string( "mapgen_id" ) ) {
        optional( jo, false, "mapgen_id", mapgen_id );
        map_regen = true;
    } else {
        optional( jo, false, "radius", radius, 3 );
        optional( jo, false, "terrain_changes", terrain_changes );
        optional( jo, false, "furn_changes", furn_changes );
    }
    optional( jo, false, "query", query, true );
    optional( jo, false, "query_msg", query_msg );
    mandatory( jo, false, "success_msg", success_msg );
    mandatory( jo, false, "redundant_msg", redundant_msg );

}

void cardreader_examine_actor::finalize() const
{
    if( allowed_flags.empty() ) {
        debugmsg( "Cardreader examine actor has no allowed card flags." );
    }

    for( const flag_id &flag : allowed_flags ) {
        if( !flag.is_valid() ) {
            debugmsg( "Cardreader uses flag %s that does not exist!", flag.str() );
        }
    }

    if( terrain_changes.empty() && furn_changes.empty() && mapgen_id.empty() ) {
        debugmsg( "Cardreader examine actor does not change either terrain or furniture" );
    }

    if( query && query_msg.empty() ) {
        debugmsg( "Cardreader is told to query, yet does not have a query message defined." );
    }

    if( allow_hacking && ( !furn_changes.empty() || terrain_changes.size() != 1 ||
                           terrain_changes.count( ter_str_id( "t_door_metal_locked" ) ) != 1 ||
                           terrain_changes.at( ter_str_id( "t_door_metal_locked" ) ) != ter_str_id( "t_door_metal_c" ) ) ) {
        debugmsg( "Cardreader allows hacking, but activites different that if hacked." );
    }
}

std::unique_ptr<iexamine_actor> cardreader_examine_actor::clone() const
{
    return std::make_unique<cardreader_examine_actor>( *this );
}

////////////////// Crafter ////////////////////////////////

void crafter_examine_actor::call( player *guy, const tripoint &examp ) const
{
    // Called from the processing item
    if( guy == nullptr ) {
        if( active ) {
            process( examp );
        } else {
            debugmsg( "Called crafter_examine_actor with no player!" );
            return;
        }
    }

    show_options( *guy, examp );
}

int crafter_examine_actor::query_options( player &guy, const tripoint &examp ) const
{

    map &here = get_map();

    bool has_fuel = false;
    units::volume item_volume = 0_liter;
    time_duration time_left = 0_turns;
    int fuel_amount = 0;

    for( const item &it : here.i_at( examp ) ) {
        if( it.has_flag( processing_flag ) ) {
            item_volume += it.volume();
        }
        if( active && it.typeId() == fake_item ) {
            time_left = time_duration::from_turns( it.item_counter );
        }
        if( it.typeId() == fuel ) {
            has_fuel = true;
            fuel_amount += it.charges;
        }
    }

    uilist menu;
    const std::string tname = name.translated();
    menu.text = string_format( _( "Do what with this %s:" ), tname );
    menu.desc_enabled = true;

    menu.addentry( static_cast<int>( options::inspect ), true, 'i', string_format( _( "Inspect %s" ),
                   tname ) );

    if( active ) {
        menu.addentry_desc( static_cast<int>( options::disable ), can_disable, 'x',
                            disable_name.translated(),
                            disable_desc.translated() );
    } else {
        const bool has_required_fuel = fuel_amount >= min_fuel &&
                                       fuel_amount / fuel_per_liter == units::to_liter( item_volume );
        const bool has_processable_items = item_volume > 0_liter;
        std::string menu_start_msg = start_msg.translated();
        const std::string menu_start_desc = start_desc.translated();

        if( !has_processable_items ) {
            menu_start_msg = start_msg_no_items.translated();
        } else if( !has_required_fuel ) {
            menu_start_msg = start_msg_no_fuel.translated();
        }

        menu.addentry_desc( static_cast<int>( options::start ), has_required_fuel &&
                            has_processable_items, 's', menu_start_msg, menu_start_desc );

        const bool full = item_volume >= max_processed_volume;
        const std::string menu_add_msg = full ? add_full_msg.translated() : add_space_msg.translated();

        menu.addentry_desc( static_cast<int>( options::add ), !full, 'a', menu_add_msg,
                            add_desc.translated() );

        const std::string menu_remove_msg = has_processable_items ? remove_items_msg.translated() :
                                            remove_no_msg.translated();

        menu.addentry( static_cast<int>( options::remove ), has_processable_items, 'e', menu_remove_msg );

        const bool can_reload = guy.crafting_inventory().charges_of( fuel ) > 0;
        const std::string menu_reload_msg = can_reload ? reload_items_msg.translated() :
                                            reload_no_msg.translated();

        menu.addentry_desc( static_cast<int>( options::reload ), can_reload, 'r', menu_reload_msg,
                            string_format( reload_desc.translated(),
                                           fuel_per_liter, format_volume( 1_liter ), volume_units_long(), min_fuel ) );

        if( portable ) {
            menu.addentry( static_cast<int>( options::disassemble ), true, 'z', disassemble_msg.translated() );
        }
    }

    if( has_fuel ) {
        menu.addentry( static_cast<int>( options::remove_fuel ), true, 'f',
                       string_format( remove_fuel_msg.translated(),
                                      fuel_amount ) );
    }

    menu.query();

    return menu.ret;
}

void crafter_examine_actor::show_options( player &guy, const tripoint &examp ) const
{
    map_stack items = get_map().i_at( examp );
    if( active && ( items.empty() || ( items.size() == 1 && items.begin()->typeId() == fake_item ) ) ) {
        debugmsg( "%s is active, but has no items in it!", name.translated() );
        transform( examp );
        return;
    }

    options selected = static_cast<options>( query_options( guy, examp ) );

    switch( selected ) {
        case options::inspect: {
            display_info( guy, examp );
            break;
        }
        case options::start: {
            if( active ) {
                return;
            }
            activate( guy, examp );
            break;
        }
        case options::add: {
            load_items( guy, examp );
            break;
        }
        case options::remove: {
            remove_items( guy, examp );
            break;
        }
        case options::reload: {
            insert_fuel( guy, examp );
            break;
        }
        case options::remove_fuel: {
            remove_fuel( guy, examp );
            break;
        }
        case options::disable: {
            transform( examp );
            break;
        }
        case options::disassemble: {
            disassemble( examp );
            break;
        }
        case options::invalid: {
            add_msg( _( "Never mind." ) );
            break;
        }
    }
}

void crafter_examine_actor::load( const JsonObject &jo )
{
    mandatory( jo, false, "active", active );
    optional( jo, false, "portable", portable, false );
    optional( jo, false, "furniture_transform", f_transform, f_null );
    mandatory( jo, false, "processing_item", fake_item );
    optional( jo, false, "fuel", fuel, itype_id::NULL_ID() );
    optional( jo, false, "min_fuel", min_fuel, 0 );
    optional( jo, false, "fuel_per_liter", fuel_per_liter, 0 );
    optional( jo, false, "max_volume", max_processed_volume, 0_liter );
    mandatory( jo, false, "processed_flag", processing_flag );
}

void crafter_examine_actor::finalize() const
{
    if( !f_transform.id().is_valid() ) {
        debugmsg( "Crafter has no valid furn to transform into (%s is set)", f_transform.id().str() );
    }
}

std::unique_ptr<iexamine_actor> crafter_examine_actor::clone() const
{
    return std::make_unique<crafter_examine_actor>( *this );
}
