#include "shell.h"

typedef struct proc
{
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job
{
  pid_t pgid;    /* 0 if slot is free */
  proc_t *proc;  /* array of processes running in as a job */
  int nproc;     /* number of processes */
  int state;     /* changes when live processes have same state */
  char *command; /* textual representation of command line */
} job_t;

static job_t *jobs = NULL; /* array of all jobs */
static int njobmax = 1;    /* number of slots in jobs array */
static int tty_fd = -1;    /* controlling terminal file descriptor */

// Odpala sie u rodzica gdy dziecko skonczy prace!
static void sigchld_handler(int sig)
{
  int old_errno = errno;
  pid_t pid;
  int status;

  // TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
  // Bury all children that finished, saving their status in jobs.

  // Zakonczyc job, ktorego wszystkie procesy sa finished
  // tworzac job tworzymy nowa grupe procesow.
  // SIGSTOP dostaje grupa procesow bedaca w foreground
  //? safe_printf("Do rodzica: dziecko skonczylo! \n");

  while (-1 != (pid = waitpid(-1, &status, WNOHANG))) //pid = pid dziecka, ktore skonczylo
  {
    //? safe_printf("Status dziecka: %d\n", status);
    for (int i = 0; i < njobmax; i++)
    {
      // safe_printf("pgid: %d\n", jobs[i].pgid);
      // safe_printf("nproc: %d\n", jobs[i].nproc);
      // safe_printf("state: %d\n", jobs[i].state);

      // if (jobs[i].nproc != 0)
      // {
      //   safe_printf("proc[pid]: %d\n", jobs[i].proc->pid);
      //   safe_printf("proc[state]: %d\n", jobs[i].proc->state);
      // }
      if (jobs[i].nproc == 0)
      {
        jobs[i].state = FINISHED;
      }
      else
      {
        for (int j = 0; j < jobs[i].nproc; j++)
        {
          //? safe_printf("siema2");
          if (pid == jobs[i].proc->pid)
          {
            jobs[i].state = FINISHED;
            //? safe_printf("%d\n", pid);
          }
        }
      }
    }
  }

  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job)
{
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void)
{
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  return njobmax++;
}

static int allocproc(int j)
{
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

// Adds info about new job to array
int addjob(pid_t pgid, int bg)
{
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  return j;
}

static void deljob(job_t *job)
{
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to)
{
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv)
{
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++)
  {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv)
{
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

// * Returns job's state.
// * If it's finished, delete it and return exitcode through statusp.
int jobstate(int j, int *statusp)
{
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  // TODO: Handle case where job has finished.
  if (state == FINISHED)
  {
    deljob(job);
  }
  // job decided to die
  // ? printf("DUPA: %d\n", state);

  return state;
}

// Returns job's command
char *jobcmd(int j)
{
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask)
{
  if (j < 0)
  {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

  // TODO: Continue stopped job. Possibly move job to foreground slot. */

  // zadanie polega na tym, że
  // trzeba będzie użyć killpg i tam sygnał "typu" SIGCONT
  // potem jeszcze cos sprawdzamy
  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j)
{
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  // TODO: I love the smell of napalm in the morning. */
  // somebody decided that job should die
  for (int i = 0; i < jobs[j].nproc; i++)
  {
    kill(jobs[j].proc[i].pid, SIGTERM);
  }

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which)
{
  for (int j = BG; j < njobmax; j++)
  {
    if (jobs[j].pgid == 0)
      continue;

    // TODO: Report job number, state, command and exit code or signal.
    printf("Job number: %d \n", j);
    printf("Job state: %d", jobs[j].state);
    printf("Job command: %s", jobs[j].command);
  }
}

// Monitor job execution. If it gets stopped move it to background.
// When a job has finished or has been stopped move shell to foreground.
int monitorjob(sigset_t *mask)
{
  int exitcode, state;

  // TODO: Following code requires use of Tcsetpgrp of tty_fd.
  //Tcsetpgrp(tty_fd, getpgrp());

  //Tcsetpgrp()

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void)
{
  Signal(SIGCHLD, sigchld_handler);
  jobs = calloc(sizeof(job_t), 1);

  // Assume we're running in interactive mode, so move us to foreground.
  // Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  //fcntl(tty_fd, F_SETFL, O_CLOEXEC); ROBOCZO
  //Tcsetpgrp(tty_fd, getpgrp()); ROBOCZO
}

/* Called just before the shell finishes. */
void shutdownjobs(void)
{
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  // TODO: Kill remaining jobs and wait for them to finish. */

  for (int j = BG; j < njobmax; j++)
  {
    if (jobs[j].pgid == 0)
      continue;
    killjob(j);
  }

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}
