#include "iexamine_actors.h"

#include "flag.h"
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
#include "string_input_popup.h"
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

void crafter_examine_actor::display_info( player &, const tripoint &examp ) const
{
    std::string display;
    map_stack items_here = get_map().i_at( examp );

    if( active ) {
        display += colorize( active_str.translated(), c_green ) + "\n";
        time_duration time_left = 0_turns;
        for( const item &it : items_here ) {
            if( it.typeId() == fake_item ) {
                time_left = time_duration::from_turns( it.item_counter );
            }
        }
        display += string_format( _( "It will take about %s to finish." ), to_string( time_left ) ) + "\n";

    } else {
        display += colorize( string_format( _( "There is a %s here." ), name.translated() ),
                             c_green ) + "\n";
    }

    display += colorize( _( "You inspect the contents and find: " ), c_green ) + "\n";


    if( items_here.empty() ) {
        display += _( "…that it is empty." );
    } else {
        for( const item &it : items_here ) {
            display += string_format( "-> %s (%d)\n", item::nname( it.typeId(), it.charges ), it.charges );
        }
    }

    popup( display, PF_NONE );
}

static bool is_non_rotten_crafting_component( const item &it )
{
    return is_crafting_component( it ) && !it.rotten();
}

cata::optional<itype_id> crafter_examine_actor::select_item_to_load( inventory &inv ) const
{
    inv.remove_items_with( []( const item & it ) {
        return it.rotten();
    } );
    std::vector<item *> filtered = inv.items_with( [this]( const item & it ) {
        return it.has_flag( processing_flag );
    } );

    std::vector<itype_id> candidates;

    for( const item *usable : filtered ) {
        int count;
        const itype_id used = usable->typeId();
        if( usable->count_by_charges() ) {
            count = inv.charges_of( used );
        } else {
            count = inv.amount_of( used );
        }
        if( count > 0 && std::find( candidates.begin(), candidates.end(), used ) == candidates.end() ) {
            candidates.push_back( used );
        }
    }

    uilist selection_menu;
    selection_menu.text = load_query_msg.translated();
    for( const itype_id &it : candidates ) {
        selection_menu.addentry( it->nname( 1 ) );
    }

    const int selected = selection_menu.ret;
    if( selected < 0 || static_cast<size_t>( selected ) >= candidates.size() ) {
        add_msg( _( "Never mind." ) );
        return cata::nullopt;
    }

    return candidates[selected];
}

void crafter_examine_actor::load_items( player &guy, const tripoint &examp ) const
{
    if( active ) {
        guy.add_msg_if_player( no_load_active_msg.translated() );
    }

    inventory inv = guy.crafting_inventory();
    cata::optional<itype_id> which = select_item_to_load( inv );
    if( !which ) {
        return;
    }
    const itype_id chosen = *which;
    int count;
    if( chosen->count_by_charges() ) {
        count = inv.charges_of( chosen );
    } else {
        count = inv.amount_of( chosen );
    }
    count = std::min( count, item( chosen ).volume() / free_volume( examp ) );
    const std::string message = string_format( _( "Insert how many of the %s?" ), chosen->nname( 1 ) );
    int amount = string_input_popup().title( message ).text( std::to_string( count ) ).only_digits(
                     true ).query_int();

    if( amount == 0 ) {
        add_msg( _( "Never mind." ) );
        return;
    } else if( amount > count ) {
        amount = count;
    }

    std::vector<item_comp> comps;
    comps.push_back( item_comp( chosen, amount ) );

    comp_selection<item_comp> selected_comps = guy.select_item_component( comps, 1, inv, true,
            is_non_rotten_crafting_component );
    std::list<item> removed = guy.consume_items( selected_comps, 1, is_non_rotten_crafting_component );

    for( const item &current : removed ) {
        get_map().add_item( examp, current );
        guy.mod_moves( -guy.item_handling_cost( current ) );
        add_msg( m_info, _( "You place %d %s in the %s." ), amount, item::nname( current.typeId(), amount ),
                 name.translated() );
    }

    guy.invalidate_crafting_inventory();
}

void crafter_examine_actor::insert_fuel( player &guy, const tripoint &examp ) const
{
    iexamine::reload_furniture( guy, examp );
}

void crafter_examine_actor::activate( player &, const tripoint &examp ) const
{
    map &here = get_map();

    std::vector<const item *> rejects;
    //item *fuel_item = nullptr;
    for( const item &it : here.i_at( examp ) ) {
        if( it.typeId() == fake_item ) {
            continue;
        }
        if( !it.has_flag( processing_flag ) ) {
            rejects.push_back( &it );
        }
    }

    if( !rejects.empty() ) {
        add_msg( m_info, _( "The %s cannot be activated while it contains %s!" ), name.translated(),
        enumerate_as_string( rejects.begin(), rejects.end(), []( const item * rejected ) {
            return rejected->tname();
        } ) );
    }
}

void crafter_examine_actor::process( const tripoint &examp ) const
{
    map &here = get_map();
    map_stack items_here = here.i_at( examp );

    bool done = false;
    for( map_stack::iterator it = items_here.begin(); it != items_here.end(); ) {
        if( it->typeId() == fake_item && ( it->age() >= crafting_time || it->item_counter == 0 ) ) {
            done = true;
            break;
        }
    }

    if( !done ) {
        return;
    }
}

void crafter_examine_actor::produce_items( const tripoint &examp,
        const time_point &start_time ) const
{
    map &here = get_map();
    map_stack items = here.i_at( examp );
    if( items.empty() ) {
        transform( examp );
        return;
    }

    for( item &it : items ) {
        if( it.has_flag( processing_flag ) ) {
            if( it.get_comestible()->smoking_result.is_empty() ) {
                it.unset_flag( flag_PROCESSING );
            } else {
                it.calc_rot_while_processing( 6_hours );

                item result( it.get_comestible()->smoking_result, start_time + 6_hours, it.charges );

                // Set flag to tell set_relative_rot() to calc from bday not now
                result.set_flag( flag_PROCESSING_RESULT );
                result.set_relative_rot( it.get_relative_rot() );
                result.unset_flag( flag_PROCESSING_RESULT );

                recipe rec;
                result.inherit_flags( it, rec );
                if( !result.has_flag( flag_NUTRIENT_OVERRIDE ) ) {
                    // If the item has "cooks_like" it will be replaced by that item as a component.
                    if( !it.get_comestible()->cooks_like.is_empty() ) {
                        // Set charges to 1 for stacking purposes.
                        it = item( it.get_comestible()->cooks_like, it.birthday(), 1 );
                    }
                    result.components.push_back( it );
                    // Smoking is always 1:1, so these must be equal for correct kcal/vitamin calculation.
                    result.recipe_charges = it.charges;
                    result.set_flag_recursive( flag_COOKED );
                }

                it = result;
            }
        }
    }

    transform( examp );
}

void crafter_examine_actor::transform( const tripoint &examp ) const
{
    map &here = get_map();
    if( f_transform != f_null ) {
        here.furn_set( f_transform );
    }
}

void crafter_examine_actor::disassemble( const tripoint &examp ) const
{
    if( !portable ) {
        debugmsg( "Tried to disassemble crafter that cannot be disassembled!" );
        return;
    }

    map &here = get_map();
    here.add_item_or_charges( examp, disassemble_item );
    here.furn_set( examp, f_null );
}

static void remove_from_crafter( player &user, const tripoint &examp,
                                 std::function<bool ( const map_stack::iterator & )> selector, const translation &msg )
{
    map &here = get_map();
    map_stack items_here = here.i_at( examp );

    for( map_stack::iterator it = items_here.begin(); it != items_here.end(); ) {
        if( selector( it ) ) {
            const int handling_cost = -user.item_handling_cost( *it );

            add_msg( msg.translated(), it->tname() );
            here.add_item_or_charges( user.pos(), *it );
            it = items_here.erase( it );
            user.mod_moves( handling_cost );
        } else {
            ++it;
        }
    }
}

void crafter_examine_actor::remove_items( player &user, const tripoint &examp ) const
{
    remove_from_crafter( user, examp, [this]( const map_stack::iterator & it ) {
        return it->typeId() != fuel && it->typeId() != fake_item;
    }, remove_items_msg );
}

void crafter_examine_actor::remove_fuel( player &user, const tripoint &examp ) const
{
    remove_from_crafter( user, examp, [this]( const map_stack::iterator & it ) {
        return it->typeId() == fuel;
    }, remove_fuel_msg );
}

units::volume crafter_examine_actor::free_volume( const tripoint &examp ) const
{
    map &here = get_map();
    map_stack items_here = here.i_at( examp );

    units::volume free = max_processed_volume;

    for( const item &it : items_here ) {
        if( it.typeId() != fake_item && it.typeId() != fuel ) {
            free -= it.volume();
        }
    }

    return free;
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
