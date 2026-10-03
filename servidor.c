#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(){
    int sd, new_sd;
    //Estructuras para guardar la dirección del servidor y del cliente
    struct sockaddr_in sockname, from;
    char buffer[100];
    socklen_t from_len;
    int salir = 0;

    int on = 1;

    int recibidos;

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
    sockname.sin_port = htons(2026);
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

    while(1){
        //Aceptar conextion entrante (accept bloquea hasta que llega una conexión, y luego crea un nuevo socket para atender al cliente, y se rellena la estructura from con la dirección del cliente)
        //Ahora sd sigue escuchando nuevas conexiones, y new_sd es el socket que se usará para atender al cliente
        if((new_sd = accept(sd, (struct sockaddr *)&from, &from_len)) == -1){
            perror("Error en la operación accept");
            exit(1);
        }

        printf("Cliente conectado desde %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

        bzero(buffer, sizeof(buffer));
        strcpy(buffer, "+Ok. Usuario conectado\n");
        if(send(new_sd, buffer, sizeof(buffer), 0) == -1){
            perror("Error en la operación send");
            exit(1);
        }

        // Bucle de comunicación con el cliente
        do{
			salir = 0;
			
            // Recibir datos del cliente
            // Limpiar el buffer antes de recibir datos con bzero, para evitar que queden datos de mensajes anteriores
			bzero(buffer, sizeof(buffer));

            recibidos = recv(new_sd, buffer, sizeof(buffer), 0);

			if(recibidos == -1){
                perror("Error en la operación recv");
                exit(1);
            }

            // Si el cliente se desconecta, recv devuelve 0
            if(recibidos == 0){
                printf("Cliente desconectado\n");
                salir = 1;
            }

            else{
                buffer[strcspn(buffer, "\n")] = 0; // Eliminar el salto de línea del final del mensaje

                if(strcmp(buffer, "SALIR") == 0){
                    salir = 1;
                }
                
                printf("El mensaje recibido fue: %s\n",buffer);

                bzero(buffer, sizeof(buffer));
                strcpy(buffer, "+Ok. Mensaje recibido\n");
                if(send(new_sd, buffer, sizeof(buffer), 0) == -1){
                    perror("Error en la operación send");
                    exit(1);
                }
            }
		}while(!salir);
			
		close(new_sd);
	}

    close(sd);
	return 0;
}