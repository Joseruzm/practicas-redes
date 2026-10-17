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

#define MSG_SIZE 200

// Fases en las que peude estar el cliente (nos va a servir para decidir que menu mostrarle al jugador)
#define FASE_LOGIN 0	// Cliente sin identificar
#define FASE_MENU 1		// Cliente identificado y puede iniciar partida
#define FASE_ESPERA 2	// Cliente esperando rival
#define FASE_JUEGO 3	// Cliente jugando una partida

// Metodo que muestra los comandos disponibles en cada fase 
void mostrarMenu(int fase);

// Metodo que devuelve 1 si el mensaje empieza por el texto indicado
int empiezaPor(const char * mensaje, const char * prefijo);

int main ( )
{
  
	int fase = FASE_LOGIN;
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

				// Según el mensaje del servidor, cambiamos de fase y mostramos los comandos que tocan
                if(empiezaPor(buffer, "+Ok. Usuario conectado")){
                    fase = FASE_LOGIN;
                    mostrarMenu(fase);
                }
                else if(empiezaPor(buffer, "+Ok. Usuario correcto")){
                    printf("-> Ahora escribe: PASSWORD [contraseña]\n");
                }
                else if(empiezaPor(buffer, "+Ok. Usuario registrado")){
                    printf("-> Ahora identifícate con USUARIO y PASSWORD\n");
                }
                else if(empiezaPor(buffer, "-Err. Error en la validación")){
                    printf("-> Vuelve a empezar con USUARIO [usuario]\n");
                }
                else if(empiezaPor(buffer, "+Ok. Usuario validado")){
                    fase = FASE_MENU;
                    mostrarMenu(fase);
                }
                else if(empiezaPor(buffer, "+Ok. Esperando otro jugador")){
                    fase = FASE_ESPERA;
                    mostrarMenu(fase);
                }
                else if(empiezaPor(buffer, "+Ok. Empieza la partida")){
                    fase = FASE_JUEGO;
                    mostrarMenu(fase);
                }
                else if(empiezaPor(buffer, "+Ok. Fin de la partida") || empiezaPor(buffer, "+Ok. Tu oponente ha terminado la partida")){
                    fase = FASE_MENU;
                    mostrarMenu(fase);
                }
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

                // AYUDA no se envía al servidor: es un comando solo del cliente
                if(strcmp(buffer, "AYUDA") == 0){
                    mostrarMenu(fase);
                }
                else{
                    if(send(sd, buffer, sizeof(buffer), 0) == -1){
                        perror("Error al enviar el mensaje");
                        exit(1);
                    }

                    if(strcmp(buffer, "SALIR") == 0)
                        fin = 1;
                }
            }
        }
    }while(fin == 0);
	
	// Cerrar el socket del cliente	
	close(sd);
	return 0;
}
		

// Muestra por pantalla los comandos que se pueden usar en cada fase
void mostrarMenu(int fase){
    printf("\n=============== COMANDOS DISPONIBLES ===============\n");

    if(fase == FASE_LOGIN){
        printf("  %-42s%s\n", "USUARIO [usuario]", "Identificarte con tu usuario");
        printf("  %-42s%s\n", "PASSWORD [password]", "Tu contraseña (tras USUARIO)");
        printf("  %-42s%s\n", "REGISTRO [usuario] [password]", "Crear un usuario nuevo");
    }
    else if(fase == FASE_MENU){
        printf("  %-42s%s\n", "INICIAR-PARTIDA", "Buscar una partida");
    }
    else if(fase == FASE_ESPERA){
        printf("  Esperando rival... (puedes salir con SALIR)\n");
    }
    else{
        printf("  %-42s%s\n", "ESTADO", "Ver el tablero");
        printf("  %-42s%s\n", "CONQUISTAR [origen] [destino] [unidades]", "Ocupar un territorio neutral");
        printf("  %-42s%s\n", "ATACAR [origen] [destino] [unidades]", "Atacar un territorio del rival");
        printf("  %-42s%s\n", "REFORZAR [origen] [destino] [unidades]", "Mover unidades entre los tuyos");
        printf("  %-42s%s\n", "PASAR", "Terminar tu turno");
        printf("\n  Los territorios son letra+número (A1 ... E5).\n");
        printf("  El origen debe ser tuyo y el destino, un vecino directo\n");
        printf("  (arriba, abajo, izquierda o derecha; no en diagonal).\n");
        printf("  Ejemplo: CONQUISTAR B2 B3 10 (B2 tuyo con 10+ unidades, B3 libre)\n\n");
    }

    printf("  %-42s%s\n", "AYUDA", "Mostrar este menú");
    printf("  %-42s%s\n", "SALIR", "Salir del juego");
    printf("====================================================\n");
}

// Devuelve 1 si el mensaje empieza por el texto indicado, 0 si no
int empiezaPor(const char * mensaje, const char * prefijo){
    return strncmp(mensaje, prefijo, strlen(prefijo)) == 0;
}