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

typedef uint32_t	storage_version_t;

int storage_dump(const char * restrict const identifier, const storage_version_t * restrict const version, const void * restrict const object, const size_t size);
int storage_fetch(const char * restrict const identifier, storage_version_t * restrict const version, void * restrict const object, const size_t size);

#endif /* rwchcd_storage_h */
