/* ============================================================
 * 文件: src/port/gh_os_port.h
 * 功能: 跨平台底层 OS 抽象 (Windows / POSIX)
 * 说明: 封装线程、互斥锁、休眠、时间获取等操作，屏蔽平台差异。
 * ============================================================ */

#ifndef GH_OS_PORT_H
#define GH_OS_PORT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32

#include <windows.h>
#include <process.h>

/* Mutex 封装 (CRITICAL_SECTION) */
typedef CRITICAL_SECTION gh_mutex_t;
#define GH_MUTEX_INIT(m)    InitializeCriticalSection(m)
#define GH_MUTEX_LOCK(m)    EnterCriticalSection(m)
#define GH_MUTEX_UNLOCK(m)  LeaveCriticalSection(m)
#define GH_MUTEX_DESTROY(m) DeleteCriticalSection(m)

/* Thread 封装 */
typedef HANDLE gh_thread_t;
#define GH_SLEEP_MS(ms)     Sleep(ms)

/* Win 下提供单调时间毫秒（利用 GetTickCount64） */
static inline uint32_t gh_platform_now_ms(void) {
    return (uint32_t)GetTickCount64();
}

#else

#include <pthread.h>
#include <unistd.h>
#include <time.h>

/* Mutex 封装 */
typedef pthread_mutex_t gh_mutex_t;
#define GH_MUTEX_INIT(m)    pthread_mutex_init(m, NULL)
#define GH_MUTEX_LOCK(m)    pthread_mutex_lock(m)
#define GH_MUTEX_UNLOCK(m)  pthread_mutex_unlock(m)
#define GH_MUTEX_DESTROY(m) pthread_mutex_destroy(m)

/* Thread 封装 */
typedef pthread_t gh_thread_t;

/* POSIX 下的延迟与时间 */
#define GH_SLEEP_MS(ms)     usleep((ms) * 1000)

static inline uint32_t gh_platform_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

#endif /* _WIN32 */

#endif /* GH_OS_PORT_H */
