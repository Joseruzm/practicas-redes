#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#define MAX_MSG 100
#define PUERTO 2026
#define MAX_CLIENTES 10

void salirCliente(int socket, fd_set * readfds, int * numClientes, int arrayClientes[]);

int main(){
    int sd, new_sd;
    
    
    fd_set readfds, auxfds;
    int salida;
    int arrayClientes[MAX_CLIENTES];
    int numClientes = 0;
    int i;
    int on = 1;
    int recibidos;
    
    struct sockaddr_in sockname, from;
    socklen_t from_len;
    char buffer[MAX_MSG];

    // Abrimos el socket
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if(sd == -1){
        perror("No se puede abrir el socket servidor\n");
        exit(1);
    }

    if(setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1){
        perror("Error en la operación setsockopt");
        exit(1);
    }

    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(PUERTO);
    sockname.sin_addr.s_addr = INADDR_ANY;

    // Asociamos el socket a la dirección del servidor (a partir de aquí todo lo que llegue al puerto 2026 se enviará a este socket)
    if(bind(sd, (struct sockaddr *)&sockname, sizeof(sockname)) == -1){
        perror("Error en la operación bind\n");
        exit(1);
    }

    from_len = sizeof(from);

    if(listen(sd, 1) == -1){
        perror("Error en la operación listen");
        exit(1);
    }


        printf("Servidor esperando conexiones...\n");

    FD_ZERO(&readfds);
    FD_ZERO(&auxfds);
    FD_SET(sd, &readfds);

    while(1){

        auxfds = readfds;

        salida = select(FD_SETSIZE, &auxfds, NULL, NULL, NULL);

        if(salida > 0){
            for(i = 0; i < FD_SETSIZE; i++){

                if(FD_ISSET(i, &auxfds)){

                    if(i == sd){
                        // Aquí van tus comentarios sobre accept
                        if((new_sd = accept(sd, (struct sockaddr *)&from, &from_len)) == -1){
                            perror("Error en la operación accept");
                        }
                        else{
                            printf("Cliente conectado desde %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

                            if(numClientes < MAX_CLIENTES){
                                arrayClientes[numClientes] = new_sd;
                                numClientes++;
                                FD_SET(new_sd, &readfds);

                                bzero(buffer, sizeof(buffer));
                                strcpy(buffer, "+Ok. Usuario conectado");
                                send(new_sd, buffer, sizeof(buffer), 0);
                            }
                            else{
                                bzero(buffer, sizeof(buffer));
                                strcpy(buffer, "-Err. Demasiados clientes conectados");
                                send(new_sd, buffer, sizeof(buffer), 0);
                                close(new_sd);
                            }
                        }
                    }
                    else{
                        // Un cliente ya conectado ha enviado algo, o se ha desconectado
                        bzero(buffer, sizeof(buffer));

                        recibidos = recv(i, buffer, sizeof(buffer), 0);

                        if(recibidos > 0){
                            buffer[strcspn(buffer, "\n")] = '\0';

                            if(strcmp(buffer, "SALIR") == 0){
                                salirCliente(i, &readfds, &numClientes, arrayClientes);
                            }
                            else{
                                printf("Cliente %d: %s\n", i, buffer);

                                // PROVISIONAL: aquí se conectará después el protocolo
                                // (USUARIO, PASSWORD, REGISTRO, INICIAR-PARTIDA...)
                                bzero(buffer, sizeof(buffer));
                                strcpy(buffer, "+Ok. Mensaje recibido");
                                send(i, buffer, sizeof(buffer), 0);
                            }
                        }
                        else{
                            if(recibidos == -1)
                                perror("Error en la operación recv");
                            else
                                printf("El cliente %d se ha desconectado\n", i);

                            salirCliente(i, &readfds, &numClientes, arrayClientes);
                        }
                    }
                }
            }
        }
    }

    close(sd);
    return 0;
}

void salirCliente(int socket, fd_set * readfds, int * numClientes, int arrayClientes[]){
    int j;

    close(socket);
    FD_CLR(socket, readfds);

    // Reestructurar el array de clientes
    for(j = 0; j < (*numClientes) - 1; j++)
        if(arrayClientes[j] == socket)
            break;
    for(; j < (*numClientes) - 1; j++)
        arrayClientes[j] = arrayClientes[j+1];

    (*numClientes)--;
}