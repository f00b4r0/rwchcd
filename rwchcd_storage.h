//
//  rwchcd_storage.h
//  rwchcd
//
//  Created by Thibaut Varene on 06/10/2016.
//  Copyright © 2016 Slashdirt. All rights reserved.
//

#ifndef rwchcd_storage_h
#define rwchcd_storage_h

#include "rwchcd.h"

int storage_dump(const char * restrict const identifier, const uint_fast32_t * restrict const version, const size_t size, const void * restrict const object);
int storage_fetch(const char * restrict const identifier, uint_fast32_t * restrict const version, const size_t size, void * restrict const object);

#endif /* rwchcd_storage_h */
