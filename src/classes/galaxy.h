
#ifndef _Galaxy
#define _Galaxy

#include "celestial.h"

class Galaxy : public CelestialObject
{
    const __uint32_t magic_g = 0x7e17edfe;              // Do not remove.

    public:
    Galaxy();
};

#endif