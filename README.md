Date: 11/13/2026

Project 2 CSMC with Semaphores, Mutexes
=======================================

Additional Test Configurations:
1. ./csmc 5 1 10 10
    PURPOSE: High-demand, high-capacity with low supply of tutors.
        Like a stress test.
    OBSERVATION: Program passes test case. The visit counts increase
        sequentially (which is expected as there is only one tutor).
        The no_chair log has a count of 0 because 10 chairs suffice
        for 5 students at all times.

2. ./csmc 1 5 5 10
    PURPOSE: To see if tutors cycle through or if it always goes to one
        tutor, since there is only one student.
    OBSERVATION: The tutor changes every visit, sequentially, showing the
        counting semaphore working as it should. Additionally, the visit
        count also increases in proper order for each type of log. The
        coordinator log shows the tutor id and the tutor log matches that.

3. ./csmc 50 50 1 5
    PURPOSE: To see if one-to-one mapping of students to tutors prevent no_chair
        log as much as possible, especially given the low 1 chair constraint.
    OBSERVATION: An average of 25 no_chair logs happened, at 250 total sessions.
        Overall, the trend was of 0 to 4 visits in the visit count part of the
        logs. Each visit had 50 TUTOR_DONE logs meaning no student was dropped.


Synchronization Design:
The run_simulation function initializes 3 types of semaphores. Coordinator semaphore
allows coordinator to sleep unless there is a student thread that arrives (student
successfully takes a chair and then signals to the coordinator to wake them up), which
is when they will process their arrival and place them in their correct place in the
priority queue. The process of adding into a queue from the waiting area is locked with
a mutex lock. The tutor semaphore is a counting semaphore that can be signaled up to
num_tutors times because a specific tutor is not required, just any tutor that is available.
This is signaled by the coordinator thread once they process a student arrival into the
priority queue. Then the tutor thread picks out the student with the highest priority and
signals that specific student's semaphore to bring them out of waiting room. During this
picking process, the mutex lock is applied to prevent new student arrivals from invalidating
the picking of the highest priority student. Student semaphores are one per student thread.
When a student arrives, it also acquires the lock to attempt to enter the waiting area.
It keeps the lock until it posts to the coordinator that it has arrived through
coordinator's semaphore.


Bugs Discovered & Solved:
Circular queue makes it so there is one less usable spot in memory as you intend.
It was discovered during the ./csmc 4 1 4 4 test case, which is when I added 1
to num_chairs in the dynamic allocation and during the mod operation to calculate
circular indices when head and tail moves during increments and decrements of the
waiting_area and priority_queues.
Determining where to place the break condition of "isSimulationDone" in the
coordinator and tutor threads was also difficult, resulting in many segfaults
and "too many logs" errors from the logger.h/logger.c header files. I was
initially thinking that these conditions should be the very last thing in the
both of their respective while(1) loops. But this turned out to be wrong, since
some students could still be in tutoring on their side, so the simulation
couldn't have been done. Hence, I decided to sem_post both of their semaphores
in the run_simulation function after the student_threads had joined. This would
for sure bring them to pthread_exit(0). For the tutor semaphore, it had to be
posted to num_tutors times because that would be the only way to reach all the
tutors, as one signal only reaches one thread.
Another small logging bug was with the TUTOR_DONE log. I was using the global
array keeping track of student visit counts and taking the value directly from
there without any lock, which led to race conditions if the student side
updated early (since the sleep time was randomly generated on both sides). So
I saved the visit count in a local variable in the mutex lock protected
section and called that variable in the log.
Initially, I was having trouble understanding that the waiting area was
a separate data structure maintain apart from the priority queue. But then
I figured out that the waiting area is important for initial claiming of seats
after which the coordinator processes the assignment into the priority queue.
