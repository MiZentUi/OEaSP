#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void report_error(const char* op)
{
    int err = errno;
    std::fprintf(stderr, "operation: %s\nerrno: %d\ndescription: %s\n", op, err,
                 std::strerror(err));
}

static double now_seconds()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        report_error("clock_gettime");
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static inline uint64_t splitmix64_next(uint64_t& state)
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline double uniform01(uint64_t& state)
{
    return (double)(splitmix64_next(state) >> 11) * (1.0 / 9007199254740992.0);
}

static uint64_t monte_carlo_hits(uint64_t n, uint64_t seed)
{
    uint64_t state = seed;
    uint64_t hits = 0;
    for (uint64_t i = 0; i < n; ++i)
    {
        double x = uniform01(state);
        double y = uniform01(state);
        if (x * x + y * y <= 1.0)
            ++hits;
    }
    return hits;
}

static inline uint64_t make_seed(int worker_idx)
{
    return 0xC0FFEEULL + (uint64_t)worker_idx * 0x9E3779B97F4A7C15ULL;
}

static uint64_t compute_reference(uint64_t N, int K)
{
    uint64_t total = 0;
    for (int i = 0; i < K; ++i)
    {
        uint64_t n = N / K + ((uint64_t)i < (N % K) ? 1 : 0);
        total += monte_carlo_hits(n, make_seed(i));
    }
    return total;
}

struct RunResult
{
    double creation_time = 0.0;
    double work_time = 0.0;
    double total_time = 0.0;
    uint64_t hits = 0;
    bool ok = false;
};

static RunResult run_processes(int K, uint64_t N)
{
    RunResult res;

    int (*pipes)[2] = (int (*)[2])std::calloc(K, sizeof(int[2]));
    pid_t* pids = (pid_t*)std::calloc(K, sizeof(pid_t));
    if (!pipes || !pids)
    {
        report_error("calloc");
        std::free(pipes);
        std::free(pids);
        return res;
    }
    for (int i = 0; i < K; ++i)
    {
        pipes[i][0] = pipes[i][1] = -1;
        pids[i] = -1;
    }

    double t0 = now_seconds();
    bool fatal = false;

    for (int i = 0; i < K && !fatal; ++i)
    {
        if (pipe(pipes[i]) != 0)
        {
            report_error("pipe");
            fatal = true;
            break;
        }

        std::fflush(stdout);
        pid_t pid = fork();
        if (pid < 0)
        {
            report_error("fork");
            ::close(pipes[i][0]);
            pipes[i][0] = -1;
            ::close(pipes[i][1]);
            pipes[i][1] = -1;
            fatal = true;
            break;
        }

        if (pid == 0)
        {
            ::close(pipes[i][0]);
            for (int j = 0; j < i; ++j)
            {
                if (pipes[j][0] >= 0)
                    ::close(pipes[j][0]);
                if (pipes[j][1] >= 0)
                    ::close(pipes[j][1]);
            }

            uint64_t my_n = N / K + ((uint64_t)i < (N % K) ? 1 : 0);
            uint64_t seed = make_seed(i);

            std::printf("proc %d pid=%d started\n", i, (int)getpid());
            std::fflush(stdout);

            uint64_t hits = monte_carlo_hits(my_n, seed);

            ssize_t w = ::write(pipes[i][1], &hits, sizeof(hits));
            if (w != (ssize_t)sizeof(hits))
                _exit(2);
            ::close(pipes[i][1]);
            _exit(0);
        }

        pids[i] = pid;
        ::close(pipes[i][1]);
        pipes[i][1] = -1;
    }

    double t_after_create = now_seconds();
    res.creation_time = t_after_create - t0;

    if (fatal)
    {
        for (int i = 0; i < K; ++i)
            if (pids[i] > 0)
                ::kill(pids[i], SIGTERM);
        for (int i = 0; i < K; ++i)
        {
            if (pids[i] > 0)
            {
                int st;
                while (waitpid(pids[i], &st, 0) < 0 && errno == EINTR)
                {
                }
            }
            if (pipes[i][0] >= 0)
                ::close(pipes[i][0]);
            if (pipes[i][1] >= 0)
                ::close(pipes[i][1]);
        }
        std::free(pipes);
        std::free(pids);
        return res;
    }

    uint64_t total_hits = 0;
    bool gather_ok = true;
    for (int i = 0; i < K; ++i)
    {
        uint64_t hits = 0;
        ssize_t r;
        do
        {
            r = ::read(pipes[i][0], &hits, sizeof(hits));
        } while (r < 0 && errno == EINTR);
        if (r != (ssize_t)sizeof(hits))
        {
            if (r < 0)
                report_error("read");
            else
                std::fprintf(stderr, "short read from proc %d\n", i);
            gather_ok = false;
        }
        else
            total_hits += hits;
        ::close(pipes[i][0]);
        pipes[i][0] = -1;
    }
    double t_after_work = now_seconds();
    res.work_time = t_after_work - t_after_create;
    res.total_time = t_after_work - t0;
    res.hits = total_hits;

    bool all_ok = true;
    for (int i = 0; i < K; ++i)
    {
        int status = 0;
        pid_t w;
        do
        {
            w = waitpid(pids[i], &status, 0);
        } while (w < 0 && errno == EINTR);
        if (w < 0)
        {
            report_error("waitpid");
            all_ok = false;
            continue;
        }

        if (WIFEXITED(status))
        {
            int code = WEXITSTATUS(status);
            std::printf("proc %d pid=%d exited code=%d\n", i, (int)pids[i],
                        code);
            if (code != 0)
                all_ok = false;
        }
        else if (WIFSIGNALED(status))
        {
            std::printf("proc %d pid=%d signaled signal=%d\n", i, (int)pids[i],
                        WTERMSIG(status));
            all_ok = false;
        }
        pids[i] = -1;
    }

    std::free(pipes);
    std::free(pids);
    res.ok = all_ok && gather_ok;
    return res;
}

struct ThreadArg
{
    int idx;
    uint64_t n;
    uint64_t seed;
    uint64_t hits;
};

static void* thread_worker(void* arg)
{
    ThreadArg* a = (ThreadArg*)arg;
    long tid = syscall(SYS_gettid);
    std::printf("thread %d tid=%ld started\n", a->idx, tid);
    std::fflush(stdout);

    a->hits = monte_carlo_hits(a->n, a->seed);

    std::printf("thread %d tid=%ld finished code=0\n", a->idx, tid);
    std::fflush(stdout);
    return nullptr;
}

static RunResult run_threads(int K, uint64_t N)
{
    RunResult res;

    pthread_t* tids = (pthread_t*)std::calloc(K, sizeof(pthread_t));
    ThreadArg* args = (ThreadArg*)std::calloc(K, sizeof(ThreadArg));
    if (!tids || !args)
    {
        report_error("calloc");
        std::free(tids);
        std::free(args);
        return res;
    }

    double t0 = now_seconds();
    int created = 0;
    bool fatal = false;

    for (int i = 0; i < K; ++i)
    {
        args[i].idx = i;
        args[i].n = N / K + ((uint64_t)i < (N % K) ? 1 : 0);
        args[i].seed = make_seed(i);
        args[i].hits = 0;

        int rc = pthread_create(&tids[i], nullptr, thread_worker, &args[i]);
        if (rc != 0)
        {
            errno = rc;
            report_error("pthread_create");
            fatal = true;
            break;
        }
        created++;
    }

    double t_after_create = now_seconds();
    res.creation_time = t_after_create - t0;

    uint64_t total = 0;
    bool all_ok = true;
    for (int i = 0; i < created; ++i)
    {
        void* retval = nullptr;
        int rc = pthread_join(tids[i], &retval);
        if (rc != 0)
        {
            errno = rc;
            report_error("pthread_join");
            all_ok = false;
            continue;
        }
        total += args[i].hits;
    }

    double t_after_work = now_seconds();
    res.work_time = t_after_work - t_after_create;
    res.total_time = t_after_work - t0;
    res.hits = total;
    res.ok = !fatal && all_ok;

    std::free(tids);
    std::free(args);
    return res;
}

static double median_of(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static void print_stats(const std::vector<double>& v)
{
    if (v.empty())
        return;
    std::vector<double> s = v;
    std::sort(s.begin(), s.end());
    double mean = 0.0;
    for (double x : v)
        mean += x;
    mean /= v.size();
    std::printf("median %.6f  mean %.6f  min %.6f  max %.6f\n", s[s.size() / 2],
                mean, s.front(), s.back());
}

int main(int argc, char** argv)
{
    uint64_t N = 100000000ULL;

    if (argc > 1)
    {
        char* end = nullptr;
        errno = 0;
        unsigned long long val = std::strtoull(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' || val == 0)
        {
            std::fprintf(stderr, "invalid N: %s\n", argv[1]);
            std::fprintf(stderr, "usage: %s [N]\n", argv[0]);
            return 1;
        }
        N = (uint64_t)val;
    }

    std::printf("N = %llu\n\n", (unsigned long long)N);

    {
        double t0 = now_seconds();
        uint64_t seq_hits = monte_carlo_hits(N, make_seed(0));
        double dt = now_seconds() - t0;
        double pi = 4.0 * (double)seq_hits / (double)N;
        std::printf("sequential: hits=%llu pi=%.10f time=%.6f\n\n",
                    (unsigned long long)seq_hits, pi, dt);
    }

    const int Ks[] = {1, 2, 4, 8};
    const int NUM_K = 4;
    const int REPEATS = 3;

    struct Stat
    {
        std::vector<double> times;
        double creation = 0.0, work = 0.0;
        uint64_t hits = 0;
        bool ok = false;
    };
    Stat proc_stat[NUM_K];
    Stat thr_stat[NUM_K];

    for (int ki = 0; ki < NUM_K; ++ki)
    {
        const int K = Ks[ki];

        uint64_t ref_hits = compute_reference(N, K);
        double ref_pi = 4.0 * (double)ref_hits / (double)N;

        std::printf("K=%d reference hits=%llu pi=%.10f\n", K,
                    (unsigned long long)ref_hits, ref_pi);

        for (int mode = 0; mode < 2; ++mode)
        {
            Stat& st = (mode == 0) ? proc_stat[ki] : thr_stat[ki];
            const char* name = (mode == 0) ? "processes" : "threads";

            for (int r = 0; r < REPEATS; ++r)
            {
                std::printf("%s run %d:\n", name, r + 1);
                RunResult res =
                    (mode == 0) ? run_processes(K, N) : run_threads(K, N);
                if (!res.ok)
                {
                    std::printf("run failed\n");
                    st.ok = false;
                    break;
                }
                std::printf("creation=%.6f work=%.6f total=%.6f\n",
                            res.creation_time, res.work_time, res.total_time);
                st.times.push_back(res.total_time);
                st.creation += res.creation_time;
                st.work += res.work_time;
                st.hits = res.hits;
                st.ok = true;
            }

            if (st.ok && !st.times.empty())
            {
                st.creation /= (double)st.times.size();
                st.work /= (double)st.times.size();
                double pi_est = 4.0 * (double)st.hits / (double)N;
                double diff = std::fabs(pi_est - ref_pi);
                std::printf("%s: hits=%llu pi=%.10f diff=%.3e\n", name,
                            (unsigned long long)st.hits, pi_est, diff);
                print_stats(st.times);
                std::printf("avg creation=%.6f avg work=%.6f\n", st.creation,
                            st.work);
            }
            std::printf("\n");
        }
    }

    std::printf("summary (median of %d runs)\n", REPEATS);
    std::printf("%-3s %-14s %-14s %-14s %-14s\n", "K", "proc_total",
                "proc_creat", "thr_total", "thr_creat");
    for (int ki = 0; ki < NUM_K; ++ki)
    {
        std::printf("%-3d %-14.6f %-14.6f %-14.6f %-14.6f\n", Ks[ki],
                    median_of(proc_stat[ki].times), proc_stat[ki].creation,
                    median_of(thr_stat[ki].times), thr_stat[ki].creation);
    }

    struct rusage ru_self, ru_child;
    if (getrusage(RUSAGE_SELF, &ru_self) == 0)
    {
        std::printf(
            "\nself:     user=%.3f sys=%.3f maxrss=%ld minflt=%ld majflt=%ld "
            "nvcsw=%ld nivcsw=%ld\n",
            ru_self.ru_utime.tv_sec + ru_self.ru_utime.tv_usec * 1e-6,
            ru_self.ru_stime.tv_sec + ru_self.ru_stime.tv_usec * 1e-6,
            ru_self.ru_maxrss, ru_self.ru_minflt, ru_self.ru_majflt,
            ru_self.ru_nvcsw, ru_self.ru_nivcsw);
    }
    if (getrusage(RUSAGE_CHILDREN, &ru_child) == 0)
    {
        std::printf(
            "children: user=%.3f sys=%.3f maxrss=%ld minflt=%ld majflt=%ld "
            "nvcsw=%ld nivcsw=%ld\n",
            ru_child.ru_utime.tv_sec + ru_child.ru_utime.tv_usec * 1e-6,
            ru_child.ru_stime.tv_sec + ru_child.ru_stime.tv_usec * 1e-6,
            ru_child.ru_maxrss, ru_child.ru_minflt, ru_child.ru_majflt,
            ru_child.ru_nvcsw, ru_child.ru_nivcsw);
    }

    return 0;
}