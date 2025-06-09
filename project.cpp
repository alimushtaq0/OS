#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <string.h>
#include <chrono>
#include <thread>

#define MAX_TASKS 5

using namespace std;
using namespace chrono;

struct Task {
    int id, arrival_time, execution_time, deadline;
    pthread_t thread;
    bool completed;
    sem_t start_permission;
};

Task tasks[MAX_TASKS];
int pipefd[2];
sem_t cpu_lock;

//Declared a variable called program_start_time, which will be used to store the time when the program begins.
steady_clock::time_point program_start_time;

void sort_tasks_by_deadline(int indices[], int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (tasks[indices[j]].deadline > tasks[indices[j + 1]].deadline) {
                int temp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = temp;
            }
        }
    }
}

void* task_function(void* arg) {
    Task* t = (Task*)arg;
    sem_wait(&t->start_permission); // Wait for scheduler

    sem_wait(&cpu_lock);

    // Get the current time, and calculate how many seconds have passed since the program started.
    auto now = steady_clock::now();
    int current_time = duration_cast<seconds>(now - program_start_time).count(); 

    string msg_prefix = "[Time " + to_string(current_time) + "s] ";
    string msg;

    if (current_time > t->deadline) {
        msg = msg_prefix + "Task " + to_string(t->id) + " skipped (deadline already missed).\n";
        write(pipefd[1], msg.c_str(), msg.length()); //Sends the message into a pipe (a connection between threads).This message will be picked up by the logger
        //Sthread, which prints it on the screen.
        sem_post(&cpu_lock);
        return nullptr;
    }

    this_thread::sleep_for(seconds(t->execution_time));
    now = steady_clock::now();
    int end_time = duration_cast<seconds>(now - program_start_time).count();

    if (end_time > t->deadline) {
        msg = msg_prefix + "Task " + to_string(t->id) + " missed its deadline (finished at " + to_string(end_time) + "s).\n";
    } else {
        msg = msg_prefix + "Task " + to_string(t->id) + " completed before deadline (finished at " + to_string(end_time) + "s).\n";
    }

    write(pipefd[1], msg.c_str(), msg.length());
    t->completed = true;
    sem_post(&cpu_lock);

    return nullptr;
}

void* logger_function(void* arg) {
    char buffer[256];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int n = read(pipefd[0], buffer, sizeof(buffer) - 1);
        if (n > 0) {
            cout << "[Logger] " << buffer;
        }
    }
    return nullptr;
}

void* scheduler_function(void* arg) {
    int ready_queue[MAX_TASKS];
    int ready_count = 0;
    bool launched[MAX_TASKS] = {false};

    int total_launched = 0;

    while (total_launched < MAX_TASKS) {
        auto now = steady_clock::now();
        int elapsed = duration_cast<seconds>(now - program_start_time).count();

        for (int i = 0; i < MAX_TASKS; i++) {
            if (!launched[i] && tasks[i].arrival_time <= elapsed) {
                ready_queue[ready_count++] = i;
                launched[i] = true;
            }
        }

        if (ready_count > 0) {
            sort_tasks_by_deadline(ready_queue, ready_count);

            int next_task = ready_queue[0];

            // Shift queue left
            for (int j = 1; j < ready_count; j++) {
                ready_queue[j - 1] = ready_queue[j];
            }
            ready_count--;

            sem_post(&tasks[next_task].start_permission);
            this_thread::sleep_for(seconds(tasks[next_task].execution_time));
            total_launched++;
        } else {
            this_thread::sleep_for(milliseconds(100));  // avoid busy waiting
        }
    }

    return nullptr;
}

int main() {
    tasks[0] = {1, 0, 2, 6}; // id;arrival_time;execution_time;deadline;
    tasks[1] = {3, 1, 2, 11};
    tasks[2] = {2, 2, 1, 5};
    tasks[3] = {4, 3, 3, 7};
    tasks[4] = {5, 4, 2, 6};

    if (pipe(pipefd) == -1) {
        perror("Pipe failed");
        return 1;
    }

    sem_init(&cpu_lock, 0, 1);
    for (int i = 0; i < MAX_TASKS; i++) {
        sem_init(&tasks[i].start_permission, 0, 0);
    }

    program_start_time = steady_clock::now();

    pthread_t logger, scheduler;
    pthread_create(&logger, NULL, logger_function, NULL);

    for (int i = 0; i < MAX_TASKS; i++) {
        pthread_create(&tasks[i].thread, NULL, task_function, &tasks[i]);
    }

    pthread_create(&scheduler, NULL, scheduler_function, NULL);
    pthread_join(scheduler, NULL);

    for (int i = 0; i < MAX_TASKS; i++) {
        pthread_join(tasks[i].thread, NULL);
    }

    this_thread::sleep_for(seconds(2));
    pthread_cancel(logger);
    pthread_join(logger, NULL);

    sem_destroy(&cpu_lock);
    for (int i = 0; i < MAX_TASKS; i++) {
        sem_destroy(&tasks[i].start_permission);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    return 0;
}


