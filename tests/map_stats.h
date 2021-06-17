#pragma once
#ifndef CATA_TESTS_MAP_STATS_H
#define CATA_TESTS_MAP_STATS_H

#include <map>

#include "map.h"
#include "type_id.h"

struct spawn_point;

struct map_statblock {
    int count = 0;

    std::unordered_map<ter_id, int64_t> ters;
    std::unordered_map<furn_id, int64_t> furns;
    std::unordered_map<mtype_id, int64_t> mons;
    std::unordered_map<itype_id, int64_t> items;
};

class map_statistics
{
        std::unordered_map<ter_id, int64_t> ters;
        std::unordered_map<furn_id, int64_t> furns;
        std::unordered_map<mtype_id, int64_t> mons;
        std::unordered_map<itype_id, int64_t> items;

        std::unordered_map<oter_id, map_statblock> oter_data;

        void add_to_record( const oter_id &oter, const ter_id &ter, const furn_id &furn,
                            const std::vector<mtype_id> &mon_list, const std::vector<itype_id> &item_list );
        void record( const oter_id &oter, const ter_id &ter );
        void record( const oter_id &oter, const furn_id &furn );
        void record( const oter_id &oter, const mtype_id &mon );
        void record( const oter_id &oter, const itype_id &it );

    public:
        void log( const oter_id &oter, tinymap &data, const std::vector<spawn_point> &mon_list );
        void report();
};

#endif
