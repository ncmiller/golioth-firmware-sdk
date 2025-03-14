#include "golioth_sys.h"
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#define TAG "golioth_sys_linux"

/*--------------------------------------------------
 * Time
 *------------------------------------------------*/

void golioth_sys_msleep(uint32_t ms) {
    struct timespec to_sleep = {ms / 1000, (ms % 1000) * 1000000};
    while ((nanosleep(&to_sleep, &to_sleep) == -1) && (errno == EINTR))
        ;
}

uint64_t golioth_sys_now_ms(void) {
    // Return time relative to when the program started
    static struct timespec start_spec;
    static uint64_t start_ms;
    if (!start_spec.tv_sec) {
        clock_gettime(CLOCK_REALTIME, &start_spec);
        start_ms = (start_spec.tv_sec * 1000 + start_spec.tv_nsec / 1000000);
    }

    struct timespec now_spec;
    clock_gettime(CLOCK_REALTIME, &now_spec);
    uint64_t now_ms = (now_spec.tv_sec * 1000 + now_spec.tv_nsec / 1000000);

    return now_ms - start_ms;
}

/*--------------------------------------------------
 * Semaphores
 *------------------------------------------------*/

golioth_sys_sem_t golioth_sys_sem_create(uint32_t sem_max_count, uint32_t sem_initial_count) {
    sem_t* sem = (sem_t*)golioth_sys_malloc(sizeof(sem_t));
    int err = sem_init(sem, 0, sem_initial_count);
    if (err) {
        GLTH_LOGE(TAG, "sem_init errno: %d", errno);
        golioth_sys_free(sem);
        assert(false);
        return NULL;
    }
    return (golioth_sys_sem_t)sem;
}

bool golioth_sys_sem_take(golioth_sys_sem_t sem, int32_t ms_to_wait) {
    int err;

    if (ms_to_wait >= 0) {
        struct timespec now_spec;
        clock_gettime(CLOCK_REALTIME, &now_spec);
        uint64_t now_ms = (now_spec.tv_sec * 1000 + now_spec.tv_nsec / 1000000);
        uint64_t abs_timeout_ms = now_ms + ms_to_wait;

        struct timespec abs_timeout = {
                .tv_sec = abs_timeout_ms / 1000,
                .tv_nsec = (abs_timeout_ms % 1000) * 1000000,
        };

        while ((err = sem_timedwait(sem, &abs_timeout)) == -1 && errno == EINTR) {
            continue;  // Restart if interrupt by signal
        }
    } else {
        err = sem_wait(sem);
    }

    if (err) {
        return false;
    }

    return true;
}

bool golioth_sys_sem_give(golioth_sys_sem_t sem) {
    return (0 == sem_post(sem));
}

void golioth_sys_sem_destroy(golioth_sys_sem_t sem) {
    sem_t* s = (sem_t*)sem;
    if (s) {
        sem_destroy(s);
    }
    golioth_sys_free(sem);
}

/*--------------------------------------------------
 * Software Timers
 *------------------------------------------------*/

// Wrap timer_t to also capture user's config
typedef struct {
    timer_t timer;
    golioth_sys_timer_config_t config;
} wrapped_timer_t;

static void on_timer(int sig, siginfo_t* si, void* uc) {
    wrapped_timer_t* wt = (wrapped_timer_t*)si->si_value.sival_ptr;
    if (wt->config.fn) {
        wt->config.fn(wt, wt->config.user_arg);
    }
}

golioth_sys_timer_t golioth_sys_timer_create(golioth_sys_timer_config_t config) {
    static bool initialized = false;
    const int signo = SIGRTMIN;

    if (!initialized) {
        // Install the signal handler
        struct sigaction sa = {
                .sa_flags = SA_SIGINFO,
                .sa_sigaction = on_timer,
        };
        sigemptyset(&sa.sa_mask);
        if (sigaction(signo, &sa, NULL) < 0) {
            GLTH_LOGE(TAG, "sigaction errno: %d", errno);
            return NULL;
        }

        initialized = true;
    }

    // Note: config.name is unused
    wrapped_timer_t* wt = (wrapped_timer_t*)golioth_sys_malloc(sizeof(wrapped_timer_t));
    wt->config = config;
    int err = timer_create(
            CLOCK_REALTIME,
            &(struct sigevent){
                    .sigev_notify = SIGEV_SIGNAL,
                    .sigev_signo = signo,
                    .sigev_value.sival_ptr = wt,
            },
            &wt->timer);
    if (err) {
        goto error;
    }

    struct itimerspec spec = {
            .it_interval =
                    {
                            .tv_sec = config.expiration_ms / 1000,
                            .tv_nsec = (config.expiration_ms % 1000) * 1000000,
                    },
    };

    err = timer_settime(wt->timer, 0, &spec, NULL);
    if (err) {
        goto error;
    }

    return (golioth_sys_timer_t)wt;

error:
    GLTH_LOGE(TAG, "timer_create errno: %d", errno);
    golioth_sys_free(wt);
    return NULL;
}

bool golioth_sys_timer_start(golioth_sys_timer_t timer) {
    wrapped_timer_t* wt = (wrapped_timer_t*)timer;

    struct timespec spec = {
            .tv_sec = wt->config.expiration_ms / 1000,
            .tv_nsec = (wt->config.expiration_ms % 1000) * 1000000,
    };

    struct itimerspec ispec = {
            .it_interval = spec,
            .it_value = spec,
    };

    int err = timer_settime(wt->timer, 0, &ispec, NULL);
    if (err) {
        GLTH_LOGE(TAG, "timer_start errno: %d", errno);
        return false;
    }

    return true;
}

bool golioth_sys_timer_reset(golioth_sys_timer_t timer) {
    wrapped_timer_t* wt = (wrapped_timer_t*)timer;

    struct timespec spec = {
            .tv_sec = wt->config.expiration_ms / 1000,
            .tv_nsec = (wt->config.expiration_ms % 1000) * 1000000,
    };

    struct itimerspec ispec = {
            .it_interval = spec,
            // disarm by setting it_value to zero
    };


    int err = timer_settime(wt->timer, 0, &ispec, NULL);
    if (err) {
        GLTH_LOGE(TAG, "timer_reset disarm errno: %d", errno);
        return false;
    }

    return golioth_sys_timer_start(wt);
}

void golioth_sys_timer_destroy(golioth_sys_timer_t timer) {
    wrapped_timer_t* wt = (wrapped_timer_t*)timer;
    if (!wt) {
        return;
    }
    timer_delete(wt->timer);
    golioth_sys_free(wt);
}

/*--------------------------------------------------
 * Threads
 *------------------------------------------------*/

// Wrap pthread due to incompatibility with thread function type.
//   pthread fn returns void*
//   golioth_sys_thread_fn_t returns void
typedef struct {
    pthread_t pthread;
    golioth_sys_thread_fn_t fn;
    void* user_arg;
} wrapped_pthread_t;

static void* pthread_callback(void* arg) {
    wrapped_pthread_t* wt = (wrapped_pthread_t*)arg;
    assert(wt);
    assert(wt->fn);
    wt->fn(wt->user_arg);
}

golioth_sys_thread_t golioth_sys_thread_create(golioth_sys_thread_config_t config) {
    // Intentionally ignoring from config:
    //      name
    //      stack_size
    //      prio
    wrapped_pthread_t* wt = (wrapped_pthread_t*)golioth_sys_malloc(sizeof(wrapped_pthread_t));

    wt->fn = config.fn;
    wt->user_arg = config.user_arg;

    int err = pthread_create(&wt->pthread, NULL, pthread_callback, wt);
    if (err) {
        GLTH_LOGE(TAG, "pthread_create errno: %d", errno);
        golioth_sys_free(wt);
        assert(false);
        return NULL;
    }

    return (golioth_sys_thread_t)wt;
}

void golioth_sys_thread_destroy(golioth_sys_thread_t thread) {
    // Do nothing.
    //
    // Thread will be automatically destroyed when/if the parent
    // process exits.
}
