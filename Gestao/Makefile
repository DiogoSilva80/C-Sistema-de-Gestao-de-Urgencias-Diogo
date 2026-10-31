CC = gcc
CFLAGS = -Wall -pthread -g
LIBS = -lrt

# ALVO PRINCIPAL: Ao escrever 'make', ele agora procura este alvo 'all'
# e garante que tanto o hospital como o sensor são criados.
all: hospital sensor

# Compila o hospital (junta os 4 ficheiros .c num único executável)
hospital: main.c admission.c triage.c doctor.c common.h
	$(CC) $(CFLAGS) main.c admission.c triage.c doctor.c -o hospital $(LIBS)

# Compila o sensor
sensor: sensor.c common.h
	$(CC) $(CFLAGS) sensor.c -o sensor

# Limpeza: Apaga os executáveis, o pipe e também os ficheiros .o antigos
clean:
	rm -f hospital sensor hospital_pipe *.o