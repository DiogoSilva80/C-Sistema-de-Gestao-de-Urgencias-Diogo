// Diogo António Mesquita Ferrão da Silva 2024199827
// João Pedro Augusto Palmeiro	2023223918


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include "common.h"

HospitalData *shm_data;

sem_t sem_triage_fill, sem_triage_empty;
sem_t sem_mutex_triage; 
sem_t sem_mutex_stats;  

mqd_t mq_doctor;
char *log_map; 
int log_offset = 0; 
pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER;

// Configs
int conf_n_triage = 2;
int conf_n_doctors = 2;
int conf_shift = 30;
int conf_queue_max = 5;
int conf_msq_wait_max = 10;

pid_t doctor_pids[100]; 
int doctor_pid_count = 0;

void load_config() {
    FILE *f = fopen("config.txt", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "TRIAGE_QUEUE_MAX")) conf_queue_max = atoi(strchr(line, '=') + 1);
        else if (strstr(line, "TRIAGE")) conf_n_triage = atoi(strchr(line, '=') + 1);
        else if (strstr(line, "DOCTORS")) conf_n_doctors = atoi(strchr(line, '=') + 1);
        else if (strstr(line, "SHIFT_LENGTH")) conf_shift = atoi(strchr(line, '=') + 1);
        else if (strstr(line, "MSQ_WAIT_MAX")) conf_msq_wait_max = atoi(strchr(line, '=') + 1);
    }
    fclose(f);
    if (conf_queue_max > HARD_LIMIT_QUEUE) conf_queue_max = HARD_LIMIT_QUEUE;
}

void write_log(char *msg) {
    pthread_mutex_lock(&mutex_log);
    
    // Verificação de segurança: se o log já foi fechado, imprime só no ecrã
    if (!log_map) {
        printf("%s", msg);
        pthread_mutex_unlock(&mutex_log);
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", t);
    
    char final_msg[256];
    sprintf(final_msg, "[%s] %s", time_str, msg);
    printf("%s", final_msg); 
    
    if (log_offset + strlen(final_msg) < LOG_SIZE) {
        memcpy(log_map + log_offset, final_msg, strlen(final_msg));
        log_offset += strlen(final_msg);
    }
    pthread_mutex_unlock(&mutex_log);
}

void spawn_triage(int id) {
    int *arg = malloc(sizeof(int)); *arg = id;
    pthread_t tid;
    pthread_create(&tid, NULL, triage_worker, arg);
    pthread_detach(tid);
    sem_wait(&sem_mutex_stats);
    shm_data->active_triage++;
    sem_post(&sem_mutex_stats);
}

void spawn_doctor(int id, int is_temp) {
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_IGN); 
        doctor_process(id, is_temp);
        exit(0);
    } else if (pid > 0) {
        if (doctor_pid_count < 100) doctor_pids[doctor_pid_count++] = pid;
        sem_wait(&sem_mutex_stats);
        shm_data->active_doctors++;
        sem_post(&sem_mutex_stats);
    }
}

void handle_sigusr1(int sig) {
    write_log("\n--- ESTATÍSTICAS (SIGUSR1) ---\n");
    char msg[256];
    sem_wait(&sem_mutex_stats);
    
    sprintf(msg, "Total Triados: %d | Atendidos: %d\n", shm_data->total_triados, shm_data->total_atendidos);
    write_log(msg);

    if (shm_data->total_triados > 0) {
        double avg_triage = (double)shm_data->sum_wait_triage / shm_data->total_triados;
        sprintf(msg, "Média Espera Triagem: %.2fs\n", avg_triage);
        write_log(msg);
    }
    if (shm_data->total_atendidos > 0) {
        double avg_doc = (double)shm_data->sum_wait_doctor / shm_data->total_atendidos;
        double avg_total = (double)shm_data->sum_total_time / shm_data->total_atendidos;
        sprintf(msg, "Média Espera Médico: %.2fs | Total: %.2fs\n", avg_doc, avg_total);
        write_log(msg);
    }
    write_log("------------------------------\n");
    sem_post(&sem_mutex_stats);
}

void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        sem_wait(&sem_mutex_stats);
        shm_data->active_doctors--;
        sem_post(&sem_mutex_stats);
    }
}

void handle_sigusr2(int sig) {
    if (shm_data->active_doctors < 20) spawn_doctor(900 + shm_data->active_doctors, 1); 
}

// --- CORREÇÃO DO CRASH NO SHUTDOWN ---
void handle_sigint(int sig) {
    write_log("\n>>> SIGINT RECEBIDO. A INICIAR PARAGEM... <<<\n");
    
    shm_data->running = 0;
    unlink(PIPE_NAME);
    write_log("[Main] Entrada fechada. A aguardar conclusão de pedidos...\n");

    // Esperar que as filas esvaziem
    while (1) {
        sem_wait(&sem_mutex_stats);
        int triagem = shm_data->pacientes_em_triagem;
        int espera_med = shm_data->pacientes_em_espera_med;
        sem_post(&sem_mutex_stats);

        if (triagem == 0 && espera_med == 0) {
            write_log("[Main] Todos os pedidos concluídos.\n");
            break;
        }
        sleep(1);
    }

    // Matar médicos
    for(int i=0; i<doctor_pid_count; i++) kill(doctor_pids[i], SIGKILL);
    while(wait(NULL) > 0);

    // CORREÇÃO: Mostrar estatísticas ANTES de destruir qualquer recurso
    handle_sigusr1(0);

    // Limpar IPCs
    mq_close(mq_doctor);
    mq_unlink(MQ_DOCTOR_NAME);
    
    // CORREÇÃO: Sync do log para o disco, mas NÃO fazer munmap imediato
    // se houver risco de threads de triagem ainda estarem a escrever a mensagem de saída.
    // O sistema operativo limpa a memória no exit(0). O msync é o essencial.
    msync(log_map, LOG_SIZE, MS_SYNC);
    
    // munmap(log_map, LOG_SIZE); // REMOVIDO para evitar race condition com threads a sair

    munmap(shm_data, sizeof(HospitalData));
    shm_unlink(SHM_NAME);
    
    sem_destroy(&sem_triage_fill); sem_destroy(&sem_triage_empty);
    sem_destroy(&sem_mutex_triage); sem_destroy(&sem_mutex_stats);
    
    printf(">>> SISTEMA ENCERRADO COM SUCESSO <<<\n");
    exit(0);
}

int main() {
    signal(SIGINT, handle_sigint);
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGCHLD, handle_sigchld); 
    signal(SIGUSR2, handle_sigusr2);

    load_config();

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(HospitalData));
    shm_data = mmap(NULL, sizeof(HospitalData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    memset(shm_data, 0, sizeof(HospitalData));
    shm_data->running = 1;
    shm_data->start_time = time(NULL);

    int log_fd = open(LOG_FILE, O_RDWR | O_CREAT | O_TRUNC, 0666);
    ftruncate(log_fd, LOG_SIZE);
    log_map = mmap(NULL, LOG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, log_fd, 0);
    close(log_fd);

    sem_init(&sem_triage_fill, 1, 0);
    sem_init(&sem_triage_empty, 1, conf_queue_max);
    sem_init(&sem_mutex_triage, 1, 1);
    sem_init(&sem_mutex_stats, 1, 1);

    struct mq_attr attr = {0, conf_queue_max, sizeof(Patient), 0};
    mq_unlink(MQ_DOCTOR_NAME);
    mq_doctor = mq_open(MQ_DOCTOR_NAME, O_CREAT | O_RDWR, 0666, &attr);

    write_log(">>> HOSPITAL INICIADO <<<\n");
    for(int i=0; i<conf_n_triage; i++) spawn_triage(i+1);
    for(int i=0; i<conf_n_doctors; i++) spawn_doctor(i+1, 0);

    start_admission();
    return 0;
}