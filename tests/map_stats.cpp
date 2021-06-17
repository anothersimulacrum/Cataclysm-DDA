#include "map_stats.h"

#include <fstream>

#include "game.h"
#include "item.h"
#include "mapbuffer.h"
#include "monster.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "submap.h"

static constexpr int num_overmaps = 1;

void gather_stats( overmap &om );

std::vector<tripoint_om_omt> full_overmap_pts;
map_statistics counts;

TEST_CASE( "Generate overmap statistics", "" )
{
    std::unique_ptr<overmapbuffer> ombuf = std::make_unique<overmapbuffer>();
    std::vector<overmap *> monitored;

    full_overmap_pts.reserve( OMAPX * OMAPY * OVERMAP_LAYERS );
    if( full_overmap_pts.empty() ) {
        for( int x = 0; x < OMAPX; ++x ) {
            for( int y = 0; y < OMAPY; ++y ) {
                for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
                    full_overmap_pts.emplace_back( x, y, z );
                }
            }
        }
    }

    for( int i = 0; i < num_overmaps; ++i ) {
        monitored.push_back( &ombuf->get( {i, 0} ) );
    }
    for( overmap *const check : monitored ) {
        gather_stats( *check );
    }

    counts.report();
}

void gather_stats( overmap &om )
{
    int iter = 0;
    for( const tripoint_om_omt &pt : full_overmap_pts ) {
        ++iter;
        tinymap data;
        data.generate( omt_to_sm_copy( tripoint( om.global_base_point().raw(), 0 ) + pt.raw() ),
                       calendar::turn );
        data.spawn_monsters( false );
        counts.log( om.ter( pt ), data, data.owned_submap_spawns() );

        if( iter % 10000 == 0 ) {
            MAPBUFFER.save();
            if( iter % 100000 ) {
                //printf( "submaps cleared: %d / %lu\n", iter, full_overmap_pts.size() );
                //fflush( stdout );
            }
        }
    }
}

static void log_item( std::vector<itype_id> &item_list, const item &it )
{
    if( !it.is_container_empty() ) {
        for( const item *const contained : it.contents.all_standard_items_top() ) {
            log_item( item_list, *contained );
        }
    }

    itype_id log = it.typeId();
    if( it.count_by_charges() ) {
        for( int i = 0; i < it.charges; ++i ) {
            item_list.push_back( log );
        }
    } else {
        item_list.push_back( log );
    }
}

void map_statistics::log( const oter_id &oter, tinymap &data,
                          const std::vector<spawn_point> &mon_list )
{
    if( oter.id().str() == "empty_rock" ) {
        for( int i = 0; i < ( SEEX * 2 ) * ( SEEY * 2 ); ++i ) {
            add_to_record( oter, t_rock, f_null, {}, {} );
        }
        return;
    }
    if( oter.id().str() == "open_air" ) {
        for( int i = 0; i < ( SEEX * 2 ) * ( SEEY * 2 ); ++i ) {
            add_to_record( oter, t_open_air, f_null, {}, {} );
        }
        return;
    }
    //L:printf( "\x1b[31m%s\x1b[0m\n", oter.id().c_str() );
    for( int x = 0; x < SEEX * 2; ++x ) {
        for( int y = 0; y < SEEY * 2; ++y ) {
            const point cursor( x, y );
            ter_id ter = data.ter( cursor );
            furn_id furn = data.furn( cursor );

            std::vector<mtype_id> mon;
            mon.reserve( mon_list.size() );
            for( const spawn_point &sp : mon_list ) {
                for( int i = 0; i < sp.count; ++i ) {
                    mon.push_back( sp.type );
                }
            }

            std::vector<itype_id> item_list;
            const map_stack &items_here = data.i_at( cursor );
            item_list.reserve( items_here.size() );
            for( const item &it : items_here ) {
                log_item( item_list, it );
            }

            add_to_record( oter, ter, furn, mon, item_list );
        }
    }
}

template<typename K, typename V>
static void add_one( std::unordered_map<K, V> &to, const K &key )
{
    auto iter = to.emplace( key, 1 );
    // Was actually emplaced
    if( !iter.second ) {
        // Use the provided iterator to increment
        iter.first->second++;
    }
}

template<typename K, typename V>
static V &add_if_lacking( std::unordered_map<K, V> &to, const K &key )
{
    return to.emplace( key, V() ).first->second;
}

void map_statistics::add_to_record( const oter_id &oter, const ter_id &ter, const furn_id &furn,
                                    const std::vector<mtype_id> &mon_list, const std::vector<itype_id> &item_list )
{
    add_if_lacking( oter_data, oter ).count++;

    record( oter, ter );
    if( furn != f_null ) {
        record( oter, furn );
    }
    for( const mtype_id &mon : mon_list ) {
        record( oter, mon );
    }
    for( const itype_id &it : item_list ) {
        record( oter, it );
    }
}

void map_statistics::record( const oter_id &oter, const ter_id &ter )
{
    add_one( ters, ter );
    add_one( oter_data[oter].ters, ter );
}

void map_statistics::record( const oter_id &oter, const furn_id &furn )
{
    add_one( furns, furn );
    add_one( oter_data[oter].furns, furn );
}

void map_statistics::record( const oter_id &oter, const mtype_id &mon )
{
    add_one( mons, mon );
    add_one( oter_data[oter].mons, mon );
}

void map_statistics::record( const oter_id &oter, const itype_id &it )
{
    add_one( items, it );
    add_one( oter_data[oter].items, it );
}

template<typename T>
static std::string id_to_string( const string_id<T> &id )
{
    return id.str();
}

template<typename T>
static std::string id_to_string( const int_id<T> &id )
{
    return id.id().str();
}

template<typename K, typename V>
static void log_csv( std::ofstream &out, const std::unordered_map<K, V> &data )
{
    for( const std::pair<const K, V> &datum : data ) {
        out << id_to_string( datum.first )  << ',' << datum.second << "\n";
    }
}

void map_statistics::report()
{
    mons = g->spawned;
    std::ofstream ter_report;
    std::ofstream furn_report;
    std::ofstream mon_report;
    std::ofstream item_report;

    int salt = std::chrono::system_clock::now().time_since_epoch().count() % 1000;
    ter_report.open( string_format( "global_ter_%d.csv", salt ) );
    furn_report.open( string_format( "global_furn_%d.csv", salt ) );
    mon_report.open( string_format( "global_mon_%d.csv", salt ) );
    item_report.open( string_format( "global_item_%d.csv", salt ) );

    REQUIRE( ter_report.is_open() );
    REQUIRE( furn_report.is_open() );
    REQUIRE( mon_report.is_open() );
    REQUIRE( item_report.is_open() );

    log_csv( ter_report, ters );
    log_csv( furn_report, furns );
    log_csv( mon_report, mons );
    log_csv( item_report, items );

    ter_report.close();
    furn_report.close();
    mon_report.close();
    item_report.close();
}
