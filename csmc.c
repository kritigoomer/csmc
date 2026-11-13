#include "logger.h"
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include "common_threads.h"

int NUM_CHAIRS = 0;
int NUM_HELP = 0;

int **priority_queue = NULL;
int *priority_queue_head = NULL;
int *priority_queue_tail = NULL;

int *waiting_area = NULL;
int waiting_count = 0;
int waiting_head = 0;
int waiting_tail = 0;

int *student_visits = NULL;

pthread_mutex_t student_proc_lock;
sem_t *student_sems = NULL;
sem_t tutor_sem;
sem_t coordinator_sem;

int isSimulationDone = 0;

/* Accepts decimal digits only, yielding an integer in [1, INT_MAX]. */
static int parse_positive_int(const char *text, int *result)
{
    if (text == NULL || *text == '\0') {
        return -1;
    }

    int value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        const int digit = (int)(*p - '0');
        if (value > (INT_MAX - digit) / 10) {
            return -1;
        }
        value = value * 10 + digit;
    }
    if (value == 0) {
        return -1;
    }
    *result = value;
    return 0;
}

/*
 * Coordinator Thread Function:
 * processes student arrivals
 *      places into FIFO ordering
 *      within priority bucket based on
 *      student's current number of visits.
 */
void* coordinator_thread(void* arg) {
    (void) arg;
    while(1) {
        sem_wait(&coordinator_sem); // wait for student to arrive
        Pthread_mutex_lock(&student_proc_lock);
        // critical section

        if (isSimulationDone) { // if all student threads are done, terminate
            Pthread_mutex_unlock(&student_proc_lock);
            break;
        }

        int student = waiting_area[waiting_head]; // first unattended in waiting area
        waiting_head = (waiting_head + 1) % (NUM_CHAIRS + 1);
        int priority = student_visits[student]; // priority by visit count
        // place this student at next available place of their priority queue (FIFO)
        priority_queue[priority][priority_queue_tail[priority]] = student;
        priority_queue_tail[priority] = (priority_queue_tail[priority] + 1) % (NUM_CHAIRS + 1 );

        Pthread_mutex_unlock(&student_proc_lock);
        log_event(LOG_QUEUED, student, -1, priority);
        sem_post(&tutor_sem); // signal tutor that a student is waiting
    }
    pthread_exit(0);
}

/*
 * Student Thread Function:
 * Lifecycle of a student
 *      Programs first,
 *      then tries to get a seat in the waiting area.
 *      On success, notifies coordinator to process their arrival.
 *          Then waits for tutor to tutor them.
 *      On failure, goes back to programming.
 *      Continues until recieved help enough times.
 */
void* student_thread(void* arg) {
    long id = (long) (long*) arg; // student_id
    uint seed = pthread_self(); // for thread safe random numbers

    while (student_visits[id] < NUM_HELP) { // life of student thread is bounded
        // student is programming (sleep for some time)
        long time = 2000 + (rand_r(&seed) % 8001);
        usleep(time);

        // try for tutoring
        Pthread_mutex_lock(&student_proc_lock); // attain lock
        if (waiting_count >= NUM_CHAIRS) { // no chair available
            Pthread_mutex_unlock(&student_proc_lock);
            log_event(LOG_NO_CHAIR, id, -1, student_visits[id]);
            continue; // go back to programming
        }
        else {
            waiting_count++; // take a chair
            waiting_area[waiting_tail] = id;
            waiting_tail = (waiting_tail + 1) % (NUM_CHAIRS + 1);
            Pthread_mutex_unlock(&student_proc_lock);
            log_event(LOG_SEATED, id, -1, student_visits[id]);
        }
        sem_post(&coordinator_sem); // notify coordinator of arrival
        sem_wait(&student_sems[id]); // wait for tutor to pick

        // tutor picked student:
        // student side tutoring
        time = 2000 + (rand_r(&seed) % 8001);
        usleep(time);

        // no race condition on student_visits here since only this student
        // modifies this value
        log_event(LOG_STUDENT_DONE, id, -1, student_visits[id]);
        student_visits[id]++; // visit count incremented

    }
    pthread_exit(0);
}


/*
 * Tutor Thread Function:
 * Lifecycle of a tutor
 *      waits for coordinator to inform of students waiting
 *      picks highest priority student to tutor
 *      removes them from waiting area into tutoring space
 */
void* tutor_thread(void* arg) {
    long id = (long) (long*) arg; // tutor_id
    uint seed = pthread_self(); // thread safe random num gen

    while (1) {
        sem_wait(&tutor_sem); // waits for coordinator notification of student

        Pthread_mutex_lock(&student_proc_lock);

        if (isSimulationDone) { // if all students are done, safely terminate
            Pthread_mutex_unlock(&student_proc_lock);
            break;
        }

        // picks highest priority student
        int student = -1;
        int visit = -1;
        for (int i = 0; i < NUM_HELP; i++) {
            if (priority_queue_head[i] == priority_queue_tail[i]) {
                continue; // empty priority queue
            } else {
                // first arrived student in highest priority non-empty queue
                student = priority_queue[i][priority_queue_head[i]];
                priority_queue_head[i] = (priority_queue_head[i] + 1) % (NUM_CHAIRS + 1);
                visit = student_visits[student]; // noting visit num here to prevent race conditions in TUTOR_DONE log
                log_event(LOG_ASSIGNED, student, id, visit);
                sem_post(&student_sems[student]); // bring chosen student into tutoring
                break;
            }
        }
        waiting_count--; // remove chosen student from waiting queue

        Pthread_mutex_unlock(&student_proc_lock);

        // tutor that student
        long time = 2000 + (rand_r(&seed) % 8001);
        usleep(time);

        log_event(LOG_TUTOR_DONE, student, id, visit);
    }
    pthread_exit(0);
}


/*
 * Runs the complete CSMC
 */
static int run_simulation(int num_students, int num_tutors,
                          int num_chairs, int num_help)
{
    (void)num_students;
    (void)num_tutors;
    (void)num_chairs;
    (void)num_help;

    // thread memory allocation
    pthread_t coordinator;
    pthread_t *students = malloc(sizeof(pthread_t) * num_students);
    pthread_t *tutors = malloc(sizeof(pthread_t) * num_tutors);

    // waiting and priority queue allocation
    waiting_area = malloc(sizeof(int) * (num_chairs + 1));
    NUM_CHAIRS = num_chairs;
    NUM_HELP = num_help;
    priority_queue = malloc(sizeof(int*) * num_help);
    for (int i = 0; i < num_help; i++) {
            priority_queue[i] = malloc(sizeof(int) * (num_chairs + 1));
    }
    // initializing to 0
    priority_queue_head = calloc(num_help, sizeof(int));
    priority_queue_tail = calloc(num_help, sizeof(int));
    student_visits = calloc(num_students, sizeof(int)); // everyone starts at highest priority

    // semaphore and mutex initialization
    sem_init(&coordinator_sem, 0, 0); // coordinator sem
    student_sems = malloc(sizeof(sem_t) * num_students); // student sems
    for (int i = 0; i < num_students; i++) {
        sem_init(&student_sems[i], 0, 0);
    }
    sem_init(&tutor_sem, 0, 0); // tutor sem
    Pthread_mutex_init(&student_proc_lock, NULL); // mutex

    // coordinator thread creation:
    Pthread_create(&coordinator, NULL, coordinator_thread, NULL);

    // student thread creation:
    for (int i = 0; i < num_students; i++) {
        Pthread_create(&students[i], NULL, student_thread, (void *) (long) i);
    }

    // tutor thread creation:
    for (int i = 0; i < num_tutors; i++) {
        Pthread_create(&tutors[i], NULL, tutor_thread, (void *) (long) i);
    }

    // join student threads:
    for (int i = 0; i < num_students; i++) {
        Pthread_join(students[i], NULL);
    }

    // all students done; let coordinator and tutor finish up
    isSimulationDone = 1;
    sem_post(&coordinator_sem);
    for (int i = 0; i < num_tutors; i++) {
        sem_post(&tutor_sem);
    }

    // join tutor threads:
    for (int i = 0; i < num_tutors; i++) {
        Pthread_join(tutors[i], NULL);
    }
    // join coordinator thread:
    Pthread_join(coordinator, NULL);

    // free up memory:
    free(students);
    free(tutors);
    students = NULL;
    tutors = NULL;

    free(waiting_area);
    waiting_area = NULL;

    for (int i = 0; i < num_help; i++) {
        free(priority_queue[i]);
    }
    free(priority_queue);
    priority_queue = NULL;
    free(priority_queue_head);
    priority_queue_head = NULL;
    free(priority_queue_tail);
    priority_queue_tail = NULL;

    free(student_visits);
    student_visits = NULL;

    sem_destroy(&coordinator_sem);
    sem_destroy(&tutor_sem);

    for (int i = 0; i < num_students; i++) {
        sem_destroy(&student_sems[i]);
    }
    free(student_sems);
    student_sems = NULL;


    pthread_mutex_destroy(&student_proc_lock);

    return 0;
}

int main(int argc, char *argv[])
{
    int num_students, num_tutors, num_chairs, num_help;

    if (argc != 5 ||
        parse_positive_int(argc > 1 ? argv[1] : NULL, &num_students) != 0 ||
        parse_positive_int(argc > 2 ? argv[2] : NULL, &num_tutors) != 0 ||
        parse_positive_int(argc > 3 ? argv[3] : NULL, &num_chairs) != 0 ||
        parse_positive_int(argc > 4 ? argv[4] : NULL, &num_help) != 0) {
        fprintf(stderr,
                "Usage: %s <students> <tutors> <chairs> <help>\n"
                "All four arguments must be positive integers.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (init_log(num_students, num_help) != 0) {
        perror("init_log");
        return EXIT_FAILURE;
    }

    if (run_simulation(num_students, num_tutors, num_chairs, num_help) != 0) {
        destroy_log();
        return EXIT_FAILURE;
    }

    print_log();
    destroy_log();
    return EXIT_SUCCESS;
}
