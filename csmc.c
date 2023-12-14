#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
// #include "queue.h"
// ------ import queue  ---------
#define QUEUE_SIZE 1000

typedef struct
{
    int items[QUEUE_SIZE];
    int front, rear;
} Queue;

void initQueue(Queue *q);
int isFull(Queue *q);
int isEmpty(Queue *q);
int enqueue(Queue *q, int value);
int dequeue(Queue *q);
int peek(Queue *q);
void printQueue(Queue *q);
// ------ import queue  ---------

int num_students;
int num_tutors;
int num_chair;
int empty_chair;
int num_help;
// int *students;
int *tutors;
int finished_students = 0;    // record how many students reach the num_help.
int total_requests = 0;       // total # requests (notifications sent) by students for tutoring so far
int total_tutor_sessions = 0; // total number of tutoring sessions completed so far by all the tutors
int tutor_now = 0;            // students receiving help now

sem_t mutex; // protect shared data
sem_t sem_student;
sem_t sem_coordinator;
sem_t sem_chair;
sem_t mutex_tutor_attribute;
sem_t mutex_queue;
sem_t mutex_waiting_queue;

// Queue queues[num_help];
Queue *queues = NULL;
Queue waiting_students_queue; // in order to get student in coordinator_thread and enqueue them

typedef struct
{
    int id;
    int num_help_receive;
    sem_t sem_tutor; // semaphore between student and tutor (Semaphore for signaling when tutoring is done)
} student_t;

student_t *students;

// student_t *student_array[2000];  // map student by their id
student_t **student_array;

// typedef struct {
//     int id;
//     int has_tutored;
//     sem_t sem_tutor_student;
// } tutor_t;

// tutor_t *tutors;

void *student_thread(void *stu)
{
    student_t *student = (student_t *)stu;
    // printf("stu id is : %d, he's #help received is %d\n", student->id, student->num_help_receive);
    while (1)
    {

        // check if this student reach the num_help
        if (student->num_help_receive == num_help)
        {
            sem_wait(&mutex);
            finished_students++;
            if (finished_students == num_students)
            {
                sem_post(&mutex);
                sem_post(&sem_student); // notify coordinate to terminate
            }
            sem_post(&mutex);
            // sem_post(&sem_student);  // notify coordinate to terminate
            pthread_exit(NULL); // terminate this thread
        }

        usleep(rand() % 2000); // programming part, sleep up to 2ms

        // check if there is a empty chair
        sem_wait(&sem_chair);
        if (empty_chair <= 0)
        { // no empty chair
            printf("S: Student %d found no empty chair. Will try again later.\n", student->id);
            sem_post(&sem_chair); // mutex unlockes it, by turning the value 1
            // usleep(200);
            continue; // back to program
        }

        empty_chair--; // occupy the chair
        printf("S: Student %d takes a seat. Empty chairs = %d.\n", student->id, empty_chair);
        sem_post(&sem_chair);

        // sem_wait(&mutex_queue);
        enqueue(&waiting_students_queue, student->id);
        // sem_post(&mutex_queue);

        sem_post(&sem_student); // wake up the coordinator

        sem_wait(&student->sem_tutor); // wait until finish the tutor
        // usleep(200);    // tutoring part, sleep for 0.2ms

        student->num_help_receive++; // this student's #number help +1
    }

    return NULL;
}

void *coordinator_thread()
{
    // printf("create coordinator thread\n");
    while (1)
    {

        sem_wait(&sem_student);
        int i;
        sem_wait(&mutex);
        if (finished_students == num_students)
        {
            sem_post(&mutex);
            // printf("CO : all students already reach the help limit\n");
            // wake up all the tutors then let them exit
            for (i = 0; i < num_tutors; i++)
            {
                sem_post(&sem_coordinator);
            }
            pthread_exit(NULL);
        }
        sem_post(&mutex);

        // wait for the student
        // sem_wait(&sem_student);

        // push into queue

        sem_wait(&mutex_waiting_queue);
        while (!isEmpty(&waiting_students_queue))
        {
            int stuID = dequeue(&waiting_students_queue); // get students who is waiting into queue
            sem_post(&mutex_waiting_queue);
            student_t *student = student_array[stuID]; // get student
            sem_wait(&mutex_queue);
            enqueue(&queues[student->num_help_receive], stuID); // enqueue them into priority queues
            sem_post(&mutex_queue);

            // Testing
            // int currentWaitingStudents = num_chair - empty_chair;
            // assert(currentWaitingStudents <= num_chair && "Number of students waiting cannot be greater than the number of chairs");

            sem_wait(&sem_chair);
            printf("C: Student %d with priority %d added to the queue. Waiting students now = %d. Total requests = %d\n", stuID, student->num_help_receive, num_chair - empty_chair, ++total_requests);
            sem_post(&sem_chair);
            // printf("Waiting students now = %d. ", num_chair - empty_chair);
            // printf("Total requests = %d\n", total_requests);

            // wake up the tutor
            sem_post(&sem_coordinator);
        }
    }

    return NULL;
}

void *tutor_thread(void *tut)
{
    int tutID = *(int *)tut; // convert to int
    // printf("tut id is : %d\n", tutID);

    while (1)
    {
        sem_wait(&sem_coordinator); // wait for the coordinator
        sem_wait(&mutex);
        if (finished_students == num_students)
        {
            // printf("TUT : all students already reach the help limit\n");
            sem_post(&mutex);
            pthread_exit(NULL);
        }
        sem_post(&mutex);

        // sem_wait(&sem_coordinator);  // wait for the coordinator

        // Critical Session for getting student request from queue
        sem_wait(&mutex_queue);
        // get the highest priority student
        int stuID;
        student_t *student = NULL;
        int i;
        for (i = 0; i < num_help; i++)
        {
            if (!isEmpty(&queues[i]))
            {
                stuID = dequeue(&queues[i]);    // get stuID
                student = student_array[stuID]; // get the student by ID
                break;
            }
        }

        if (student == NULL)
        { // there is no student in the queue
            sem_post(&mutex_queue);
            continue;
        }
        sem_post(&mutex_queue);

        // Critical Session for empty_chair attribute
        sem_wait(&sem_chair);
        empty_chair++;
        sem_post(&sem_chair);

        // Critical Session for tutor attribute
        sem_wait(&mutex_tutor_attribute);
        tutor_now++;
        printf("T: Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d\n", stuID, tutID, tutor_now, ++total_tutor_sessions);

        // Testing
        assert(total_tutor_sessions <= total_requests && "Total sessions tutored cannot exceed total requests");
        sem_post(&mutex_tutor_attribute);

        usleep(200); // tutoring part, sleep for 0.2ms
        printf("S: Student %d received help from Tutor %d.\n", student->id, tutID);

        // Critical Session for tutor attribute
        sem_wait(&mutex_tutor_attribute);
        tutor_now--;
        sem_post(&mutex_tutor_attribute);

        sem_post(&student->sem_tutor);
    }

    return NULL;
}

int main(int argc, char *argv[])
{

    if (argc != 5)
    {
        printf("Error # of arguments");
        return 1;
    }
    num_students = atoi(argv[1]);
    num_tutors = atoi(argv[2]);
    num_chair = atoi(argv[3]);
    num_help = atoi(argv[4]);
    empty_chair = num_chair;

    // allocate memory space for students and tutors
    students = malloc(num_students * sizeof(student_t));
    tutors = malloc(num_tutors * sizeof(int));
    queues = (Queue *)malloc(num_help * sizeof(Queue));
    student_array = malloc(num_students * sizeof(student_t *));
    if (students == NULL || tutors == NULL || queues == NULL || student_array == NULL)
    {
        printf("Not Enough Memory for students or tutors or queues");
        return 1;
    }

    // initialize queues
    initQueue(&waiting_students_queue);
    int i;
    for (i = 0; i < num_help; i++)
    {
        initQueue(&queues[i]);
    }

    sem_init(&mutex, 0, 1);
    sem_init(&sem_student, 0, 0);
    sem_init(&sem_coordinator, 0, 0);
    sem_init(&sem_chair, 0, 1);
    sem_init(&mutex_tutor_attribute, 0, 1);
    sem_init(&mutex_queue, 0, 1);
    sem_init(&mutex_waiting_queue, 0, 1);

    // allocate memory space for threads
    pthread_t student_p[num_students];
    pthread_t tutor_p[num_tutors];
    pthread_t coordinator_p;

    // create student thread
    for (i = 0; i < num_students; i++)
    {
        students[i].id = i;
        students[i].num_help_receive = 0;
        sem_init(&students[i].sem_tutor, 0, 0);
        student_array[i] = &students[i];
        pthread_create(&student_p[i], NULL, student_thread, &students[i]);
    }

    // create tutor thread
    for (i = 0; i < num_tutors; i++)
    {
        tutors[i] = i;
        pthread_create(&tutor_p[i], NULL, tutor_thread, &tutors[i]);
    }

    // create coordinator thread
    pthread_create(&coordinator_p, NULL, coordinator_thread, NULL);

    // wait every threads finished
    for (i = 0; i < num_students; i++)
    {
        pthread_join(student_p[i], NULL);
    }
    for (i = 0; i < num_tutors; i++)
    {
        pthread_join(tutor_p[i], NULL);
    }
    pthread_join(coordinator_p, NULL);

    // release the memory space
    free(students);
    free(tutors);
    free(queues);
    free(student_array);
    return 0;
}

// ------ import queue  ---------
void initQueue(Queue *q)
{
    q->front = -1;
    q->rear = -1;
}

int isFull(Queue *q)
{
    return q->rear == QUEUE_SIZE - 1;
}

int isEmpty(Queue *q)
{
    return q->front == -1;
}

int enqueue(Queue *q, int value)
{
    if (isFull(q))
        return 0;

    if (isEmpty(q))
        q->front = 0;

    q->rear++;
    q->items[q->rear] = value;
    return 1;
}

int dequeue(Queue *q)
{
    if (isEmpty(q))
        return -1;

    int item = q->items[q->front];
    q->front++;
    if (q->front > q->rear)
    {
        q->front = q->rear = -1;
    }
    return item;
}

int peek(Queue *q)
{
    if (isEmpty(q))
        return -1;

    return q->items[q->front];
}

void printQueue(Queue *q)
{
    if (isEmpty(q))
    {
        printf("Queue is empty\n");
        return;
    }
    printf("Queue contains: ");
    for (int i = q->front; i <= q->rear; i++)
        printf("%d ", q->items[i]);
    printf("\n");
}