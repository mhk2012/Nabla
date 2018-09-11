// Copyright (C) 2018 Mateusz 'DevSH' Kielan
// This file is part of the "IrrlichtBAW Engine"
// For conditions of distribution and use, see copyright notice in irrlicht.h

#ifndef __IRR_ADDRESS_ALLOCATOR_STATE_TYPES_H_INCLUDED__
#define __IRR_ADDRESS_ALLOCATOR_STATE_TYPES_H_INCLUDED__

#include "IrrCompileConfig.h"

#include "irr/core/alloc/AddressAllocatorState.h"

#include "irr/core/alloc/LinearAddressAllocator.h"

namespace irr
{
namespace core
{

//! Regular states
template<typename AddressType>
using LinearAddressAllocatorStateST = AddressAllocatorState<LinearAddressAllocatorST<AddressType> >;
template<typename AddressType, class BasicLockable>
using LinearAddressAllocatorStateMT = AddressAllocatorState<LinearAddressAllocatorMT<AddressType,BasicLockable> >;

//! Buffer States
template<class B, typename AddressType>
using LinearAddressAllocatorDriverMemoryStateST = AllocatorStateDriverMemoryAdaptor<B, LinearAddressAllocatorStateST<AddressType> >;
template<class B, typename AddressType, class BasicLockable>
using LinearAddressAllocatorDriverMemoryStateMT = AllocatorStateDriverMemoryAdaptor<B, LinearAddressAllocatorStateMT<AddressType,BasicLockable> >;

}
}

#endif // __IRR_ADDRESS_ALLOCATOR_STATE_TYPES_H_INCLUDED__