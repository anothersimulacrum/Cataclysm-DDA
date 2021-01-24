#include "reprocase.h"

#include "clone_ptr.h"
#include "mapdata.h"

struct valued {
    int one;
    int two;

    std::unique_ptr<valued> clone() const {
        return std::make_unique<valued>( *this );
    }
};

class copied_object
{
    public:
        translation a;
        cata::clone_ptr<valued> b;
};

valued mint;

void trigger_memory_leak()
{
    std::vector<copied_object> sources;
    copied_object first;
    first.a = to_translation( "lorem ipsum dolor sit amet" );
    first.b = cata::make_value<valued>( mint );
    sources.push_back( first );
    copied_object second = sources.front();

    debugmsg( second.a.translated() );
}

