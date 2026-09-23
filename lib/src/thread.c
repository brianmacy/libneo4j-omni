/* vi:set ts=4 sw=4 expandtab:
 *
 * Copyright 2016, Chris Leishman (http://github.com/cleishm)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "../../config.h"
#include "thread.h"


#ifdef HAVE_PTHREADS

unsigned long neo4j_current_thread_id(void)
{
    return (unsigned long)pthread_self();
}

#elif defined(_WIN32)

unsigned long neo4j_current_thread_id(void)
{
    return (unsigned long)GetCurrentThreadId();
}


static BOOL CALLBACK once_trampoline(PINIT_ONCE once, PVOID param,
        PVOID *context)
{
    (void)once;
    (void)context;
    void (*init_routine)(void) = (void (*)(void))(uintptr_t)param;
    init_routine();
    return TRUE;
}


int neo4j_win32_thread_once(INIT_ONCE *once, void (*init_routine)(void))
{
    if (!InitOnceExecuteOnce(once, once_trampoline,
                (PVOID)(uintptr_t)init_routine, NULL))
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

#endif
