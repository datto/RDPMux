/*
 * Copyright 2016 Datto Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef QEMU_RDP_COMMON_H
#define QEMU_RDP_COMMON_H

#include <memory>
#include <cstdbool>
#include "util/logging.h"
#include <giomm-2.4/giomm.h>

#define RDPMUX_PROTOCOL_VERSION 5

/**
 * @brief enum of message types.
 */
enum message_type {
    MSGTYPE_INVALID = 0,
    DISPLAY_UPDATE,
    DISPLAY_SWITCH,
    MOUSE,
    KEYBOARD,
    DISPLAY_UPDATE_COMPLETE,
    SHUTDOWN
};

/**
 * @brief std::make_unique from C++14
 *
 * std::make_unique backported from C++14 because it's just too useful not to have.
 *
 * @code
 * std::unique_ptr<Vec3> v1 = make_unique<Vec3>();
 * @endcode
 *
 * @returns std::unique_ptr encapsulating the object
 *
 * @param Args used to create the pointer to the object
 */
template<typename _Tp, typename... _Args>
std::unique_ptr<_Tp> make_unique(_Args &&... __args)
{
    return std::unique_ptr<_Tp>(new _Tp(std::forward<_Args>(__args)...));
}

/**
 * @brief std::make_unique from C++14
 *
 * std::make_unique backported from C++14 because it's just too useful not to have.
 *
 * @code
 * std::unique_ptr<Vec3[]> v3 = make_unique<Vec3[]>(5);
 * @endcode
 *
 * @returns std::unique_ptr encapsulating the array
 *
 * @param size of the array you wish to encapsulate
 */
template<typename _Tp>
std::unique_ptr<_Tp> make_unique(std::size_t __num)
{
    return std::unique_ptr<_Tp>(new typename std::remove_extent<_Tp>::type[__num]());
}

#endif //QEMU_RDP_COMMON_H
