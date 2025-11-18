/**
 * Copyright (C) 2021, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * - larod_client_tool -
 *
 * This application writes "Larod client tool to enable ssh developer mode, started!" to the syslog.
 * It is only to create an ACAP user to create access to ssh user in developer mode.
 */

#include <syslog.h>

/***** Main function *********************************************************/

/**
 * brief Main function.
 *
 * This main function writes "Larod client tool to enable ssh developer mode" to the syslog.
 */
int main(void) {
    /* Choose between { LOG_INFO, LOG_CRIT, LOG_WARNING, LOG_ERR }*/
    syslog(LOG_INFO, "Larod client tool to enable ssh developer mode, started!");
}