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

/**
 * @brief enum of message types.
 */
enum message_type {
    DISPLAY_UPDATE,
    DISPLAY_SWITCH,
    MOUSE,
    KEYBOARD,
    DISPLAY_UPDATE_COMPLETE,
    SHUTDOWN
};

#endif //QEMU_RDP_COMMON_H
