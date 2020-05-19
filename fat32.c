//----------------------------------------------------------------------------//

// Copyright (c) 2020 Bjørn Brodtkorb
//
// This software is provided without warranty of any kind.
// Permission is granted, free of charge, to copy and modify this
// software, if this copyright notice is included in all copies of
// the software.

#include "fat32.h"


// Private variables
static struct volume_s* first_volume;
static u32 volume_bitmask;

// Private functions