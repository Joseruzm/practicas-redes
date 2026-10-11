#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MSG_SIZE 250

int main ( )
{
  
	/*---------------------------------------------------- 
		Descriptor del socket y buffer de datos                
	-----------------------------------------------------*/
	int sd;
	struct sockaddr_in sockname;
	char buffer[MSG_SIZE];
	socklen_t len_sockname;
	
	fd_set readfds, auxfds;
	int salida;
	int recibidos;
	int fin = 0;
	
	/* --------------------------------------------------
		Se abre el socket 
	---------------------------------------------------*/
  	sd = socket (AF_INET, SOCK_STREAM, 0);
	if (sd == -1)
	{
		perror("No se puede abrir el socket cliente");
    		exit (1);	
	}

	

	/* ------------------------------------------------------------------
		Se rellenan los campos de la estructura con la IP del 
		servidor y el puerto del servicio que solicitamos
	-------------------------------------------------------------------*/
	sockname.sin_family = AF_INET;
	sockname.sin_port = htons(2026);
	sockname.sin_addr.s_addr =  inet_addr("127.0.0.1");

	/* ------------------------------------------------------------------
		Se solicita la conexión con el servidor
	-------------------------------------------------------------------*/
	len_sockname = sizeof(sockname);
	
	if (connect(sd, (struct sockaddr *)&sockname, len_sockname) == -1)
	{
		perror ("Error de conexión");
		exit(1);
	}

    FD_ZERO(&readfds);
	FD_ZERO(&auxfds);
	FD_SET(0, &readfds);
	FD_SET(sd, &readfds);

	do{
    	auxfds = readfds;

        salida = select(sd + 1, &auxfds, NULL, NULL, NULL);

        if(salida == -1){
            perror("Error en la operación select");
            exit(1);
        }

        // El servidor ha enviado algo, o ha cerrado la conexión
        if(FD_ISSET(sd, &auxfds)){
            bzero(buffer, sizeof(buffer));
            recibidos = recv(sd, buffer, sizeof(buffer), 0);

            if(recibidos == -1){
                perror("Error al recibir el mensaje");
                exit(1);
            }

            if(recibidos == 0){
                printf("El servidor ha cerrado la conexión\n");
                fin = 1;
            }
            else{
                printf("%s\n", buffer);
            }
        }

        // Se ha escrito algo por teclado
        if(FD_ISSET(0, &auxfds)){
            bzero(buffer, sizeof(buffer));

            if(fgets(buffer, sizeof(buffer), stdin) == NULL){
                fin = 1;    // Ctrl+D: se cierra la entrada
            }
            else{
                buffer[strcspn(buffer, "\n")] = '\0';

                if(send(sd, buffer, sizeof(buffer), 0) == -1){
                    perror("Error al enviar el mensaje");
                    exit(1);
                }

                if(strcmp(buffer, "SALIR") == 0)
                    fin = 1;
            }
        }
    }while(fin == 0);
	
	// Cerrar el socket del cliente	
	close(sd);
	return 0;
}
		

