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
#include <time.h>

// ============================================================================
//  CONSTANTES
// ============================================================================

// Tamaño de TODOS los mensajes que se envían/reciben 
#define MAX_MSG 200
// Puerto en el que escucha el servidor (lo exige el enunciado)
#define PUERTO 2026
// Máximo de clientes conectados a la vez (lo exige el enunciado)
#define MAX_CLIENTES 10

// Tamaño máximo del nombre de usuario (y también de la contraseña) incluido el '\0'
#define MAX_USUARIO 50

// Fichero con los usuarios
// (cada línea tiene el formato: "usuario password")
#define FICHERO_USUARIOS "usuarios.txt"

// Definimos los estados en los que puede estar un cliente
// El estado es lo que permite al servidor saber qué comandos son válidos en cada momento
#define EST_CONECTADO 0     // conectado, sin identificar
#define EST_USUARIO_OK 1    // usuario correcto, falta la contraseña
#define EST_VALIDADO 2      // identificado (usuario y contraseña correctos)
#define EST_ESPERANDO 3     // esperando rival para jugar
#define EST_JUGANDO 4       // jugando una partida

// Definimos el tablero y las unidades iniciales de cada jugador
#define FILAS 5
#define COLUMNAS 5

#define UNIDADES_INICIALES 30

// Número máximo de turnos de una partida (lo exige el enunciado)
// Contamos como "turno" cada acción de un jugador: cuando un jugador acaba su acción
// (o pasa) se termina un turno y empieza el del otro jugador
#define MAX_TURNOS 10

// Estructura que define cada casilla del tablero (cada territorio)
typedef struct {
    int propietario;  // Esta a 0 si el territorio es neutral, a 1 si pertenece al jugador 1 y a 2 si pertenece al 2
    int unidades;     // Cuántas tropas hay en el territorio y vale 0 si el territorio es neutral
} Territorio;

// ============================================================================
//  ESTRUCTURAS DE DATOS
// ============================================================================

// Toda la información que el servidor guarda de cada cliente conectado
typedef struct{
    int socket;                 // descriptor del socket con el que nos comunicamos con él
    int estado;                 // uno de los EST_* de arriba
    char usuario[MAX_USUARIO];  // nombre con el que se ha identificado
    int partida;                // índice de la partida en la que juega (-1 si no está en ninguna)
} Cliente;

// Maximo de partidas a la vez
#define MAX_PARTIDAS 4

// Información de cada partida
typedef struct{
    int activa;         // 1 si la partida está jugandose, 0 si no está activa
    int jugador1;       // socket del jugador 1 
    int jugador2;       // socket del jugador 2
    Territorio tablero[FILAS][COLUMNAS];    // Estado de cada territorio de la partida en concreto
    int turno;          // De quién es el turno ahora: 1 si le toca al jugador 1, 2 si le toca al jugador 2
    int numTurnos;      // Cuántos turnos se han completado ya en la partida (empieza en 0)
} Partida;

// ============================================================================
//  PROTOTIPOS de funciones auxiliares
// ============================================================================

// Elimina a un cliente (cierra su socket, lo saca del array y termina su partida si estaba jugando)
void salirCliente(int socket, fd_set * readfds, int * numClientes, Cliente arrayClientes[], Partida partidas[]);

// Devuelve en qué posición del array está el cliente con ese socket
int buscarCliente(int socket, Cliente arrayClientes[], int numClientes);

// Envía un mensaje de texto a un cliente
void enviarMensaje(int socket, const char * texto);

// Interpreta el mensaje de un cliente y actúa según el comando y el estado del cliente
void procesarMensaje(Cliente * cliente, char buffer[], Cliente arrayClientes[], int numClientes, Partida partidas[]);

// Comprueba si un usuario (y opcionalmente su contraseña) está en el fichero
int comprobarUsuario(char usuario[], char password[]);

// Añade un usuario nuevo al fichero
int registrarUsuario(char usuario[], char password[]);

// Busca un cliente que esté esperando rival
int buscarEnEspera(Cliente arrayClientes[], int numClientes);

// Busca un hueco libre en el array de partidas
int buscarPartidaLibre(Partida partidas[]);

// Metodo que prepara el tablero de una partida nueva
void iniciarTablero(Partida *partida);

// Método que devuelve 1 si el socket es el jugador 1 o 2 si es el jugador 2
int numeroJugador(Partida *partida, int socket);

// Método que envía a un jugador el estado del tablero, solo con lo que puede ver
void enviarEstado(int socket, Partida *partida, int jugador);

// Método que devuelve 1 si es el turno del jugador que tiene ese socket y 0 si no lo es
int esSuTurno(Partida *partida, int socket);

// Método que le avisa al jugador al que le toca ahora que es su turno
void avisarTurno(Partida *partida);

// Método que prepara el control de turnos de una partida nueva (decide quién empieza y avisa)
void iniciarTurnos(Partida *partida);

// Método que convierte un territorio escrito como texto (por ejemplo "B3") en fila y columna.
// Devuelve 1 si el territorio existe y 0 si no existe
int convertirTerritorio(const char *texto, int *fila, int *columna);

// Método que devuelve 1 si dos territorios (dados por fila y columna) son adyacentes y 0 si no lo son
int sonAdyacentes(int f1, int c1, int f2, int c2);

// Método que deja neutral un territorio si se ha quedado sin unidades(lo hacemos con una función porque lo necesitamos en varios casos)
void liberarSiVacio(Partida *partida, int fila, int columna);

int contarTerritorios(Partida *partida, int jugador);

void finalizarPartida(Partida *partida, Cliente arrayClientes[], int numClientes);

void cambiarTurno(Partida *partida, Cliente arrayClientes[], int numClientes);

// ============================================================================
//  MAIN
// ============================================================================
int main(){
    // Inicializamos el generador de números aleatorios
    srand(time(NULL));

    // sd = socket de escucha (el que recibe las conexiones nuevas)
    // new_sd = socket nuevo que devuelve accept para hablar con un cliente concreto
    int sd, new_sd;
    
    
    // readfds = conjunto de sockets que vigilamos (el original, que mantenemos siempre actualizado)
    // auxfds = copia de readfds que se pasa a select, porque select LO MODIFICA
    fd_set readfds, auxfds;
    // Valor que devuelve select (nº de descriptores con actividad)
    int salida;
    // Array con todos los clientes conectados
    Cliente arrayClientes[MAX_CLIENTES];
    // Array con las 4 partidas posibles
    Partida partidas[MAX_PARTIDAS];
    // Cuántos clientes hay conectados ahora mismo (las posiciones 0..numClientes-1 están ocupadas)
    int numClientes = 0;
    // Contador para recorrer los descriptores
    int i;
    // Valor para activar la opción SO_REUSEADDR (1 = activada)
    int on = 1;
    // Bytes que devuelve recv
    int recibidos;
    // Posición de un cliente en el array
    int pos;
    
    // sockname = dirección del servidor; from = dirección del cliente que se conecta
    struct sockaddr_in sockname, from;
    // Tamaño de la estructura from (accept lo necesita y lo rellena)
    socklen_t from_len;
    // Buffer donde se guarda lo que recibimos de los clientes
    char buffer[MAX_MSG];

    // Abrimos el socket
    // AF_INET = IPv4, SOCK_STREAM = TCP, 0 = el sistema elige el protocolo
    sd = socket(AF_INET, SOCK_STREAM, 0);
    // socket devuelve -1 si falla
    if(sd == -1){
        perror("No se puede abrir el socket servidor");
        exit(1);
    }

    // SO_REUSEADDR permite volver a lanzar el servidor justo después de cerrarlo
    // sin tener que esperar a que el puerto salga del estado TIME_WAIT
    if(setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1){
        perror("Error en la operación setsockopt");
        exit(1);
    }

    // Rellenamos la dirección del servidor:
    // familia IPv4
    sockname.sin_family = AF_INET;
    // puerto 2026; htons pasa el número al orden de bytes de red (big endian)
    sockname.sin_port = htons(PUERTO);
    // INADDR_ANY = aceptamos conexiones por cualquier interfaz de red de la máquina
    sockname.sin_addr.s_addr = INADDR_ANY;

    // Asociamos el socket a la dirección del servidor (a partir de aquí todo lo que llegue al puerto 2026 se enviará a este socket)
    if(bind(sd, (struct sockaddr *)&sockname, sizeof(sockname)) == -1){
        perror("Error en la operación bind\n");
        exit(1);
    }

    // Guardamos el tamaño de la estructura from, que accept necesita como parámetro
    from_len = sizeof(from);

    // Ponemos el socket en modo escucha (pasivo). El 2º parámetro es el tamaño
    // de la cola de conexiones pendientes de aceptar
    if(listen(sd, 1) == -1){
        perror("Error en la operación listen");
        exit(1);
    }


    printf("Servidor esperando conexiones...\n");

    // Inicializamos los dos conjuntos de descriptores a vacío
    FD_ZERO(&readfds);
    FD_ZERO(&auxfds);
    // Añadimos el socket de escucha al conjunto: así select nos avisará cuando llegue una conexión nueva
    FD_SET(sd, &readfds);

    // Ponemos a 0 todo el array de partidas, así activa = 0 en todas (todas libres).
    // sizeof(partidas) vale el tamaño de TODO el array porque está declarado en esta función
    bzero(partidas, sizeof(partidas));

    // ------------------------------------------------------------------------
    //  BUCLE PRINCIPAL DEL SERVIDOR (no termina nunca)
    // ------------------------------------------------------------------------
    while(1){

        // Copiamos el conjunto original, porque select va a modificar auxfds
        // dejando solo los descriptores que tienen actividad
        auxfds = readfds;

        // select bloquea (timeout NULL) hasta que haya actividad en algún socket del conjunto de lectura.
        // FD_SETSIZE = nº máximo de descriptores que puede vigilar select
        salida = select(FD_SETSIZE, &auxfds, NULL, NULL, NULL);

        // Si select devuelve > 0, hay al menos un socket con actividad
        if(salida > 0){
            // Recorremos todos los descriptores posibles para ver cuáles tienen actividad
            for(i = 0; i < FD_SETSIZE; i++){

                // ¿El descriptor i tiene actividad?
                if(FD_ISSET(i, &auxfds)){

                    // ---- CASO 1: actividad en el socket de escucha = cliente nuevo ----
                    if(i == sd){
                        // Aquí van tus comentarios sobre accept
                        // accept devuelve un socket nuevo para hablar con ese cliente
                        // y rellena from con su IP y puerto
                        if((new_sd = accept(sd, (struct sockaddr *)&from, &from_len)) == -1){
                            perror("Error en la operación accept");
                        }
                        else{
                            // Mostramos por pantalla la IP y puerto del cliente (ntohs pasa el puerto a orden de host)
                            printf("Cliente conectado desde %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

                            // Si hay sitio, lo guardamos en el array
                            if(numClientes < MAX_CLIENTES){
                                // Guardamos su socket en la primera posición libre
                                arrayClientes[numClientes].socket = new_sd;
                                // Al conectarse aún no se ha identificado
                                arrayClientes[numClientes].estado = EST_CONECTADO;
                                // Todavía no está en ninguna partida
                                arrayClientes[numClientes].partida = -1;
                                // Dejamos el nombre de usuario vacío
                                bzero(arrayClientes[numClientes].usuario, MAX_USUARIO);
                                // Ahora hay un cliente más
                                numClientes++;
                                // Lo añadimos al conjunto vigilado por select para enterarnos cuando envíe algo
                                FD_SET(new_sd, &readfds);

                                // Mensaje de bienvenida que pide el enunciado
                                enviarMensaje(new_sd, "+Ok. Usuario conectado");
                            }
                            // Si ya hay 10 clientes, lo rechazamos
                            else{
                                enviarMensaje(new_sd, "-Err. Demasiados clientes conectados");
                                // Cerramos su socket. No lo hemos añadido a readfds, así que no hace falta FD_CLR
                                close(new_sd);
                            }
                        }
                    }
                    // ---- CASO 2: actividad en el socket de un cliente ya conectado ----
                    else{
                        // Un cliente ya conectado ha enviado algo, o se ha desconectado
                        // Limpiamos el buffer para que no queden restos de mensajes anteriores
                        bzero(buffer, sizeof(buffer));

                        // Leemos lo que ha enviado el cliente i. Devuelve:
                        //   > 0  -> nº de bytes recibidos
                        //   == 0 -> el cliente ha cerrado la conexión
                        //   == -1 -> error
                        recibidos = recv(i, buffer, sizeof(buffer), 0);

                        // Hemos recibido datos
                        if(recibidos > 0){
                            // Quitamos el salto de línea del final (lo pone fgets en el cliente).
                            // strcspn devuelve la posición del primer '\n' y ahí ponemos el fin de cadena
                            buffer[strcspn(buffer, "\n")] = '\0';

                            // SALIR se trata aquí fuera porque es válido en cualquier estado
                            if(strcmp(buffer, "SALIR") == 0){
                                salirCliente(i, &readfds, &numClientes, arrayClientes, partidas);
                            }
                            // Cualquier otro mensaje se procesa según el estado del cliente
                            else{
                                // Buscamos al cliente en el array a partir de su socket
                                pos = buscarCliente(i, arrayClientes, numClientes);
                                // Si lo hemos encontrado, procesamos su mensaje
                                if(pos != -1)
                                    procesarMensaje(&arrayClientes[pos], buffer, arrayClientes, numClientes, partidas);
                            }
                        }
                        // recv devolvió 0 o -1: el cliente se ha ido (o hubo un error)
                        else{
                            if(recibidos == -1)
                                perror("Error en la operación recv");
                            else
                                printf("El cliente %d se ha desconectado\n", i);

                            // En ambos casos lo eliminamos del servidor
                            salirCliente(i, &readfds, &numClientes, arrayClientes, partidas);
                        }
                    }
                }
            }
        }
    }

    // (no se llega nunca aquí porque el while(1) no termina)
    close(sd);
    return 0;
}


void salirCliente(int socket, fd_set * readfds, int * numClientes, Cliente arrayClientes[], Partida partidas[]){
    // pos = posición del cliente que se va; posRival = posición de su rival
    // p = índice de la partida; socketRival = socket del rival
    int j, pos, posRival, p, socketRival;

    // Localizamos al cliente en el array
    pos = buscarCliente(socket, arrayClientes, *numClientes);

    // Si estaba jugando, se termina la partida y se avisa al rival
    if(pos != -1 && arrayClientes[pos].estado == EST_JUGANDO){
        // Índice de la partida en la que estaba
        p = arrayClientes[pos].partida;

        // Averiguamos cuál de los dos jugadores es el rival: el que NO es el que se va
        if(partidas[p].jugador1 == socket)
            socketRival = partidas[p].jugador2;
        else
            socketRival = partidas[p].jugador1;

        // Buscamos al rival en el array de clientes
        posRival = buscarCliente(socketRival, arrayClientes, *numClientes);

        // Si el rival sigue conectado, lo devolvemos al estado "validado"
        // (puede volver a pedir otra partida) y le avisamos
        if(posRival != -1){
            arrayClientes[posRival].estado = EST_VALIDADO;
            arrayClientes[posRival].partida = -1;
            enviarMensaje(socketRival, "+Ok. Tu oponente ha terminado la partida");
        }

        // Marcamos la partida como libre para que se pueda reutilizar
        partidas[p].activa = 0;
    }

    // Cerramos el socket del cliente que se va
    close(socket);
    // Lo quitamos del conjunto que vigila select
    FD_CLR(socket, readfds);

    // Reestructurar el array de clientes
    // 1º bucle: avanzamos j hasta la posición del cliente que se va
    for(j = 0; j < (*numClientes) - 1; j++)
        if(arrayClientes[j].socket == socket)
            break;
    // 2º bucle: desde esa posición, movemos cada cliente una posición a la izquierda
    // para tapar el hueco (si era el último no hace nada)
    for(; j < (*numClientes) - 1; j++)
        arrayClientes[j] = arrayClientes[j+1];

    // Hay un cliente menos
    (*numClientes)--;
}


int buscarCliente(int socket, Cliente arrayClientes[], int numClientes){
    int j;

    // Recorremos solo las posiciones ocupadas
    for(j = 0; j < numClientes; j++){
        // Si el socket coincide, devolvemos su posición
        if(arrayClientes[j].socket == socket){
            return j;
        }
    }
    
    // Si hemos llegado aquí, no estaba
    return -1;
   }


void enviarMensaje(int socket, const char * texto){
    char buffer[MAX_MSG];

    // Dejamos todo el buffer a ceros (así lo que sobra tras el texto son '\0')
    bzero(buffer, sizeof(buffer));
    // Copiamos el texto, como mucho MAX_MSG-1 caracteres para que siempre quede el '\0' final
    strncpy(buffer, texto, sizeof(buffer) - 1);

    // Enviamos el buffer completo (MAX_MSG bytes). send devuelve -1 si falla
    if(send(socket, buffer, sizeof(buffer), 0) == -1)
        perror("Error en la operación send");
}


void procesarMensaje(Cliente * cliente, char buffer[], Cliente arrayClientes[], int numClientes, Partida partidas[]){
    // comando = primera palabra del mensaje; arg1, arg2, arg3 = las siguientes palabras
    char comando[MAX_MSG], arg1[MAX_MSG], arg2[MAX_MSG], arg3[MAX_MSG];
    // campos = nº de palabras del mensaje
    // hueco = índice de partida libre; rival = posición en el array del cliente que esperaba
    int campos, hueco, rival, jugador;

    // Limpiamos las cadenas para que no tengan basura
    bzero(comando, sizeof(comando));
    bzero(arg1, sizeof(arg1));
    bzero(arg2, sizeof(arg2));
    bzero(arg3, sizeof(arg3));

    // campos = cuántas palabras tiene el mensaje (comando + argumentos)
    // sscanf con %s separa por espacios y devuelve cuántas ha podido leer.
    // Así "REGISTRO ana 1234" -> comando="REGISTRO", arg1="ana", arg2="1234", campos=3
    campos = sscanf(buffer, "%s %s %s %s", comando, arg1, arg2, arg3);

    // ---------------------------------------------------------------- USUARIO
    if(strcmp(comando, "USUARIO") == 0){

        // Solo es válido si aún no se ha identificado (estado CONECTADO)
        if(cliente->estado != EST_CONECTADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // Debe tener exactamente 2 palabras (USUARIO + nombre) y el nombre debe caber en el array
        else if(campos != 2 || strlen(arg1) >= MAX_USUARIO){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: USUARIO [usuario]");
        }
        // Con password NULL solo se comprueba que el usuario exista en el fichero
        else if(comprobarUsuario(arg1, NULL)){
            // Guardamos el nombre para usarlo cuando llegue el PASSWORD
            strcpy(cliente->usuario, arg1);
            // Pasamos al estado "falta la contraseña"
            cliente->estado = EST_USUARIO_OK;
            enviarMensaje(cliente->socket, "+Ok. Usuario correcto");
        }
        // El usuario no existe
        else{
            enviarMensaje(cliente->socket, "-Err. Usuario incorrecto");
        }
    }

    // --------------------------------------------------------------- PASSWORD
    else if(strcmp(comando, "PASSWORD") == 0){

        // Solo es válido justo después de un USUARIO correcto
        if(cliente->estado != EST_USUARIO_OK){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // Debe tener exactamente 2 palabras (PASSWORD + contraseña)
        else if(campos != 2){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: PASSWORD [password]");
        }
        // Comprobamos que el usuario guardado y esta contraseña coinciden en el fichero
        else if(comprobarUsuario(cliente->usuario, arg1)){
            // Ya está identificado: puede iniciar partidas
            cliente->estado = EST_VALIDADO;
            enviarMensaje(cliente->socket, "+Ok. Usuario validado");
        }
        // Contraseña incorrecta
        else{
            // Volvemos al principio: tendrá que mandar USUARIO otra vez
            cliente->estado = EST_CONECTADO;
            enviarMensaje(cliente->socket, "-Err. Error en la validación");
        }
    }

    // --------------------------------------------------------------- REGISTRO
    else if(strcmp(comando, "REGISTRO") == 0){

        // Solo se puede registrar quien aún no se ha identificado
        if(cliente->estado != EST_CONECTADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // Debe tener 3 palabras (REGISTRO + usuario + password) y ambos deben caber en MAX_USUARIO
        else if(campos != 3 || strlen(arg1) >= MAX_USUARIO || strlen(arg2) >= MAX_USUARIO){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: REGISTRO [usuario] [password]");
        }
        // Control de colisiones: si el usuario ya existe en el fichero, error
        else if(comprobarUsuario(arg1, NULL)){
            enviarMensaje(cliente->socket, "-Err. El usuario ya existe");
        }
        // Si no existe, lo añadimos al fichero
        else if(registrarUsuario(arg1, arg2)){
            enviarMensaje(cliente->socket, "+Ok. Usuario registrado");
        }
        // No se pudo escribir en el fichero
        else{
            enviarMensaje(cliente->socket, "-Err. No se ha podido registrar el usuario");
        }
    }

    // ------------------------------------------------------- INICIAR-PARTIDA
    else if(strcmp(comando, "INICIAR-PARTIDA") == 0){

        // Solo puede iniciar partida quien está validado (no si ya espera o ya juega)
        if(cliente->estado != EST_VALIDADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // El comando no lleva argumentos: solo 1 palabra
        else if(campos != 1){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: INICIAR-PARTIDA");
        }
        else{
            // Buscamos un hueco libre en el array de partidas (-1 si están las 4 ocupadas)
            hueco = buscarPartidaLibre(partidas);

            // Límite de 4 partidas simultáneas
            if(hueco == -1){
                enviarMensaje(cliente->socket, "-Err. No hay partidas disponibles, inténtalo más tarde");
            }
            else{
                // Miramos si ya hay alguien esperando rival
                rival = buscarEnEspera(arrayClientes, numClientes);

                if(rival == -1){
                    // Nadie esperaba: este cliente se queda esperando
                    cliente->estado = EST_ESPERANDO;
                    enviarMensaje(cliente->socket, "+Ok. Esperando otro jugador");
                }
                else{
                    // Ya había uno esperando: se forma la pareja
                    // Rellenamos la partida: el que esperaba es el jugador 1 y el que acaba de llegar el jugador 2
                    partidas[hueco].activa = 1;
                    partidas[hueco].jugador1 = arrayClientes[rival].socket;
                    partidas[hueco].jugador2 = cliente->socket;

                    // Iniciamos el tablero como partida nueva
                    iniciarTablero(&partidas[hueco]);

                    // Actualizamos el estado y la partida de los dos jugadores
                    arrayClientes[rival].estado = EST_JUGANDO;
                    arrayClientes[rival].partida = hueco;
                    cliente->estado = EST_JUGANDO;
                    cliente->partida = hueco;

                    // Avisamos a ambos de que empieza la partida
                    enviarMensaje(arrayClientes[rival].socket, "+Ok. Empieza la partida");
                    enviarMensaje(cliente->socket, "+Ok. Empieza la partida");

                    // Decidimos quién empieza y avisamos a los dos jugadores
                    iniciarTurnos(&partidas[hueco]);
                }
            }
        }
    }

    else if(strcmp(comando, "ESTADO") == 0){

        // Solo tiene sentido dentro de una partida
        if(cliente->estado != EST_JUGANDO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // El comando no lleva argumentos
        else if(campos != 1){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: ESTADO");
        }
        else{
            // Averiguamos si este cliente es el jugador 1 o el 2 de su partida
            jugador = numeroJugador(&partidas[cliente->partida], cliente->socket);

            // Le enviamos el tablero tal y como debe verlo él
            enviarEstado(cliente->socket, &partidas[cliente->partida], jugador);
        }
    }

    // ------------------------------------------------------------------ PASAR
    else if(strcmp(comando, "PASAR") == 0){

        // Solo tiene sentido dentro de una partida
        if(cliente->estado != EST_JUGANDO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // El comando no lleva argumentos
        else if(campos != 1){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: PASAR");
        }
        // Solo se puede pasar cuando es tu turno
        else if(!esSuTurno(&partidas[cliente->partida], cliente->socket)){
            enviarMensaje(cliente->socket, "-Err. No es tu turno");
        }
        else{
            // Confirmamos al jugador que ha terminado su turno
            enviarMensaje(cliente->socket, "+Ok. Turno finalizado");

            // Pasamos el turno al otro jugador (esto también le avisa a él)
            cambiarTurno(&partidas[cliente->partida], arrayClientes, numClientes);
        }
    }

    // ------------------------------------------------------------- CONQUISTAR
    else if(strcmp(comando, "CONQUISTAR") == 0){
        // filaO, colO = fila y columna del territorio de origen; filaD, colD = las del destino
        int filaO, colO, filaD, colD;
        // unidades = nº de unidades que el jugador quiere trasladar (long porque lo lee strtol)
        long unidades;
        // fin = apunta al primer carácter que strtol no ha podido convertir en número
        char * fin;
        // extra = sirve para detectar si el mensaje trae una 5ª palabra de más
        char extra[MAX_MSG];
        // texto = mensaje de respuesta que se construye con sprintf
        char texto[MAX_MSG];
        // partida = la partida en la que juega el cliente (se asigna cuando ya sabemos que está jugando)
        Partida * partida;

        // Convertimos arg3 a número. Si no es un número (por ejemplo "abc" o "10x") fin no apuntará al final de la cadena.
        // Si arg3 está vacío (faltan argumentos) el resultado será 0, que también se rechaza más abajo
        unidades = strtol(arg3, &fin, 10);

        // 1) El jugador debe estar dentro de una partida
        if(cliente->estado != EST_JUGANDO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // 2) Formato: CONQUISTAR origen destino unidades -> exactamente 4 palabras (sscanf solo lee 4,
        // así que con el segundo sscanf comprobamos que no haya una 5ª), y las unidades deben ser un entero positivo
        else if(campos != 4 || sscanf(buffer, "%*s %*s %*s %*s %s", extra) == 1 || *fin != '\0' || unidades <= 0){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: CONQUISTAR [origen] [destino] [unidades]");
        }
        // 3) Debe ser su turno
        else if(!esSuTurno(&partidas[cliente->partida], cliente->socket)){
            enviarMensaje(cliente->socket, "-Err. No es tu turno");
        }
        // 4) El territorio de origen debe existir (de paso obtenemos su fila y columna)
        else if(!convertirTerritorio(arg1, &filaO, &colO)){
            enviarMensaje(cliente->socket, "-Err. El territorio de origen no existe");
        }
        // 5) El territorio de destino debe existir (de paso obtenemos su fila y columna)
        else if(!convertirTerritorio(arg2, &filaD, &colD)){
            enviarMensaje(cliente->socket, "-Err. El territorio de destino no existe");
        }
        else{
            // A partir de aquí ya sabemos que los dos territorios existen, así que podemos mirar el tablero
            partida = &partidas[cliente->partida];
            jugador = numeroJugador(partida, cliente->socket);

            // 6) El territorio de origen debe pertenecer al jugador
            if(partida->tablero[filaO][colO].propietario != jugador){
                enviarMensaje(cliente->socket, "-Err. El territorio de origen no te pertenece");
            }
            // 7) El territorio de destino debe estar sin conquistar (neutral: propietario 0)
            else if(partida->tablero[filaD][colD].propietario != 0){
                enviarMensaje(cliente->socket, "-Err. El territorio de destino no está sin conquistar");
            }
            // 8) El destino debe ser adyacente al origen
            else if(!sonAdyacentes(filaO, colO, filaD, colD)){
                enviarMensaje(cliente->socket, "-Err. Los territorios no son adyacentes");
            }
            // 9) El origen debe tener suficientes unidades
            else if(unidades > partida->tablero[filaO][colO].unidades){
                enviarMensaje(cliente->socket, "-Err. No tienes suficientes unidades en el territorio de origen");
            }
            // Todas las condiciones se cumplen: realizamos la conquista
            else{
                // Las unidades trasladadas se descuentan del territorio de origen
                partida->tablero[filaO][colO].unidades -= unidades;

                // Comprobar si está a 0 sea neutral
                liberarSiVacio(partida, filaO, colO);

                // El destino pasa a ser del jugador y se queda con las unidades trasladadas
                partida->tablero[filaD][colD].propietario = jugador;
                partida->tablero[filaD][colD].unidades = unidades;

                // Respuesta que indica el enunciado
                snprintf(texto, sizeof(texto), "+Ok. Territorio %.2s conquistado.", arg2);
                enviarMensaje(cliente->socket, texto);

                // La acción ha sido válida: el turno pasa al otro jugador (y se le avisa)
                cambiarTurno(partida, arrayClientes, numClientes);
            }
        }
    }

    // ----------------------------------------------------------- REFORZAR
    else if(strcmp(comando, "REFORZAR") == 0){

        int filaO, colO, filaD, colD;
        long unidades;
        char *fin;
        char extra[MAX_MSG];
        char texto[MAX_MSG];
        Partida *partida;

        unidades = strtol(arg3, &fin, 10);

        // El jugador debe estar dentro de una partida
        if(cliente->estado != EST_JUGANDO){
            enviarMensaje(cliente->socket,
                        "-Err. Comando no válido en este momento");
        }

        // Comprobamos el formato
        else if(campos != 4 ||
                sscanf(buffer, "%*s %*s %*s %*s %s", extra) == 1 ||
                *fin != '\0' ||
                unidades <= 0){

            enviarMensaje(cliente->socket,
                        "-Err. Formato incorrecto. Uso: REFORZAR [origen] [destino] [unidades]");
        }

        // Debe ser el turno del jugador
        else if(!esSuTurno(&partidas[cliente->partida], cliente->socket)){
            enviarMensaje(cliente->socket, "-Err. No es tu turno");
        }

        // Comprobamos el territorio de origen
        else if(!convertirTerritorio(arg1, &filaO, &colO)){
            enviarMensaje(cliente->socket,
                        "-Err. El territorio de origen no existe");
        }

        // Comprobamos el territorio de destino
        else if(!convertirTerritorio(arg2, &filaD, &colD)){
            enviarMensaje(cliente->socket,
                        "-Err. El territorio de destino no existe");
        }

        else{
            partida = &partidas[cliente->partida];

            // El origen debe pertenecer al jugador
            if(partida->tablero[filaO][colO].propietario !=
            numeroJugador(partida, cliente->socket)){

                enviarMensaje(cliente->socket,
                            "-Err. El territorio de origen no te pertenece");
            }

            // El destino también debe pertenecer al jugador
            else if(partida->tablero[filaD][colD].propietario !=
                    numeroJugador(partida, cliente->socket)){

                enviarMensaje(cliente->socket,
                            "-Err. El territorio de destino no te pertenece");
            }

            // Deben ser territorios adyacentes
            else if(!sonAdyacentes(filaO, colO, filaD, colD)){
                enviarMensaje(cliente->socket,
                            "-Err. Los territorios no son adyacentes");
            }

            // Debe haber suficientes unidades en el origen
            else if(unidades > partida->tablero[filaO][colO].unidades){
                enviarMensaje(cliente->socket,
                            "-Err. No tienes suficientes unidades en el origen");
            }

            else{
                // Quitamos las unidades del territorio de origen
                partida->tablero[filaO][colO].unidades -= unidades;

                // Si se queda sin unidades, lo dejamos neutral,
                // siguiendo la lógica usada en CONQUISTAR y ATACAR
                liberarSiVacio(partida, filaO, colO);

                // Añadimos las unidades al destino
                partida->tablero[filaD][colD].unidades += unidades;

                snprintf(texto, sizeof(texto),
                        "+Ok. Territorio %.2s reforzado.", arg2);

                enviarMensaje(cliente->socket, texto);

                // La acción válida termina el turno
                cambiarTurno(partida, arrayClientes, numClientes);
            }
        }
    }

    // ------------------------------------------------------------- ATACAR
    else if(strcmp(comando, "ATACAR") == 0){
        // filaO, colO = fila y columna del territorio de origen; filaD, colD = las del destino
        int filaO, colO, filaD, colD;
        // unidades = nº de unidades que el jugador usa en el ataque
        long unidades;
        // fin = apunta al primer carácter que strtol no ha podido convertir en número
        char * fin;
        // extra = sirve para detectar si el mensaje trae una 5ª palabra de más
        char extra[MAX_MSG];
        // texto = mensaje de respuesta que se construye con snprintf
        char texto[MAX_MSG];
        // partida = la partida en la que juega el cliente
        Partida * partida;
        // socketRival = socket del jugador al que atacamos
        int socketRival;

        unidades = strtol(arg3, &fin, 10);

        // 1) El jugador debe estar dentro de una partida
        if(cliente->estado != EST_JUGANDO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // 2) Formato: 4 palabras exactas y unidades = entero positivo
        else if(campos != 4 || sscanf(buffer, "%*s %*s %*s %*s %s", extra) == 1 || *fin != '\0' || unidades <= 0){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: ATACAR [origen] [destino] [unidades]");
        }
        // 3) Debe ser su turno
        else if(!esSuTurno(&partidas[cliente->partida], cliente->socket)){
            enviarMensaje(cliente->socket, "-Err. No es tu turno");
        }
        // 4) El territorio de origen debe existir
        else if(!convertirTerritorio(arg1, &filaO, &colO)){
            enviarMensaje(cliente->socket, "-Err. El territorio de origen no existe");
        }
        // 5) El territorio de destino debe existir
        else if(!convertirTerritorio(arg2, &filaD, &colD)){
            enviarMensaje(cliente->socket, "-Err. El territorio de destino no existe");
        }
        else{
            partida = &partidas[cliente->partida];
            jugador = numeroJugador(partida, cliente->socket);

            // 6) El territorio de origen debe pertenecer al atacante
            if(partida->tablero[filaO][colO].propietario != jugador){
                enviarMensaje(cliente->socket, "-Err. El territorio de origen no te pertenece");
            }
            // 7) No se puede atacar un territorio propio
            else if(partida->tablero[filaD][colD].propietario == jugador){
                enviarMensaje(cliente->socket, "-Err. No puedes atacar uno de tus propios territorios");
            }
            // 8) El destino debe pertenecer al rival (si es neutral no se ataca)
            else if(partida->tablero[filaD][colD].propietario == 0){
                enviarMensaje(cliente->socket, "-Err. El territorio de destino no pertenece a tu rival");
            }
            // 9) Los territorios deben ser adyacentes
            else if(!sonAdyacentes(filaO, colO, filaD, colD)){
                enviarMensaje(cliente->socket, "-Err. Los territorios no son adyacentes");
            }
            // 10) El origen debe tener suficientes unidades
            else if(unidades > partida->tablero[filaO][colO].unidades){
                enviarMensaje(cliente->socket, "-Err. No tienes suficientes unidades en el territorio de origen");
            }
            // Todas las condiciones se cumplen: se resuelve el ataque
            else{
                // Las unidades del ataque salen SIEMPRE del origen, gane o pierda el ataque
                partida->tablero[filaO][colO].unidades -= unidades;
                liberarSiVacio(partida, filaO, colO);

                // Averiguamos el socket del rival (el jugador atacado)
                if(jugador == 1)
                    socketRival = partida->jugador2;
                else
                    socketRival = partida->jugador1;

                // Ataque con éxito: solo si las unidades atacantes SUPERAN a las defensoras
                if(unidades > partida->tablero[filaD][colD].unidades){
                    // Las unidades del defensor se pierden y las atacantes ocupan el territorio
                    partida->tablero[filaD][colD].propietario = jugador;
                    partida->tablero[filaD][colD].unidades = unidades;

                    snprintf(texto, sizeof(texto), "+Ok. Has conquistado el territorio %.2s.", arg2);
                    enviarMensaje(cliente->socket, texto);

                    snprintf(texto, sizeof(texto), "+Ok. Tu territorio %.2s ha sido conquistado.", arg2);
                    enviarMensaje(socketRival, texto);
                }
                // Ataque fallido: las unidades atacantes se pierden y el defensor no cambia
                else{
                    snprintf(texto, sizeof(texto), "+Ok. Has perdido el ataque contra %.2s.", arg2);
                    enviarMensaje(cliente->socket, texto);

                    snprintf(texto, sizeof(texto), "+Ok. Tu territorio %.2s ha resistido un ataque.", arg2);
                    enviarMensaje(socketRival, texto);
                }

                // La acción ha sido válida: el turno pasa al otro jugador (y se le avisa)
                cambiarTurno(partida, arrayClientes, numClientes);
            }
        }
    }

    else{
        enviarMensaje(cliente->socket, "-Err. Comando desconocido");
    }
}


int comprobarUsuario(char usuario[], char password[]){
    // Puntero al fichero
    FILE * f;
    // u y p = usuario y contraseña de la línea que se está leyendo del fichero
    char u[MAX_USUARIO], p[MAX_USUARIO];
    // Resultado: 1 si se encuentra, 0 si no
    int encontrado = 0;

    // Abrimos el fichero en modo lectura
    f = fopen(FICHERO_USUARIOS, "r");
    // Si no existe (por ejemplo, aún no se ha registrado nadie), no hay usuarios
    if(f == NULL){
        return 0;
    }
           
    // Leemos pares "usuario password" mientras fscanf consiga leer los 2 campos.
    // %49s = máximo 49 caracteres (MAX_USUARIO-1) para no desbordar u ni p
    while(fscanf(f, "%49s %49s", u, p) == 2){
        // Coincide si el usuario es igual Y (no nos piden comprobar contraseña O la contraseña también es igual)
        if(strcmp(u, usuario) == 0 && (password == NULL || strcmp(p, password) == 0)){
            encontrado = 1;
            // Ya lo hemos encontrado, no hace falta seguir leyendo
            break;
        }
    }

    // Cerramos el fichero siempre
    fclose(f);
    return encontrado;
}


int registrarUsuario(char usuario[], char password[]){
    FILE * f;

    // Modo "a" (append): escribe al final y crea el fichero si no existe
    f = fopen(FICHERO_USUARIOS, "a");
    if(f == NULL){
        perror("Error al abrir el fichero de usuarios");
        return 0;
    }

    // Escribimos una línea con el formato "usuario password"
    fprintf(f, "%s %s\n", usuario, password);
    // Cerrar el fichero también vuelca lo escrito al disco
    fclose(f);

    return 1;
}  


int buscarEnEspera(Cliente arrayClientes[], int numClientes){
    int j;

    // Recorremos los clientes conectados
    for(j = 0; j < numClientes; j++)
        // El primero que esté en estado ESPERANDO es el elegido
        if(arrayClientes[j].estado == EST_ESPERANDO)
            return j;

    // Nadie esperando
    return -1;
}


int buscarPartidaLibre(Partida partidas[]){
    int j;

    // Recorremos las 4 posibles partidas
    for(j = 0; j < MAX_PARTIDAS; j++)
        // La primera con activa == 0 está libre
        if(partidas[j].activa == 0)
            return j;

    // Todas ocupadas
    return -1;
}

// Iniciamos el tablero para una partida nueva
// Se le asigna un territorio inicial al azar a cada jugador
void iniciarTablero(Partida * partida){
    // Coordenadas (fila, columna) del territorio inicial de cada jugador
    int f1, c1, f2, c2;

    // Todos los territorios empiezan neutrales y sin unidades
    for(int f = 0; f < FILAS; f++){
        for(int c = 0; c < COLUMNAS; c++){
            partida->tablero[f][c].propietario = 0;
            partida->tablero[f][c].unidades = 0;
        }
    }

    // Territorio inicial del jugador 1 al azar
    // rand() % FILAS da un número entre 0 y FILAS-1
    f1 = rand() % FILAS;
    c1 = rand() % COLUMNAS;

    // Territorio inicial del jugador 2 al azar
    // Repetimos hasta que sean distintos los territorios iniciales para cada jugador
    do{
        f2 = rand() % FILAS;
        c2 = rand() % COLUMNAS;
    }while(f2 == f1 && c2 == c1);

    // Cada jugador empieza con 30 unidades en su territorio
    partida->tablero[f1][c1].propietario = 1;
    partida->tablero[f1][c1].unidades = UNIDADES_INICIALES;
    partida->tablero[f2][c2].propietario = 2;
    partida->tablero[f2][c2].unidades = UNIDADES_INICIALES;
}

// Devuelve 1 o 2 según el jugador y el socket
int numeroJugador(Partida * partida, int socket){
    if(partida->jugador1 == socket){
        return 1;
    }
    return 2;
}

// Le manda a un jugador como esta el tablero, 
// con las restricciones del enunciado
void enviarEstado(int socket, Partida * partida, int jugador){
    char linea[MAX_MSG], celda[32], texto[32];
    int f, c;

    enviarMensaje(socket, "+Ok. Estado del tablero");

    // Primera línea: los números de columna
    strcpy(linea, "  ");
    for(c = 0; c < COLUMNAS; c++){
        sprintf(celda, "%-8d", c + 1);
        strcat(linea, celda);
    }
    enviarMensaje(socket, linea);

    // Una línea por cada fila del tablero
    for(f = 0; f < FILAS; f++){
        // La línea empieza con la letra de la fila
        sprintf(linea, "%c ", 'A' + f);

        for(c = 0; c < COLUMNAS; c++){
            if(partida->tablero[f][c].propietario == 0)
                strcpy(texto, "-");
            else if(partida->tablero[f][c].propietario == jugador)
                sprintf(texto, "J%d(%d)", jugador, partida->tablero[f][c].unidades);
            else
                sprintf(texto, "J%d", partida->tablero[f][c].propietario);

            // %-8s rellena con espacios hasta 8 caracteres para que las columnas queden alineadas
            sprintf(celda, "%-8s", texto);
            strcat(linea, celda);
        }

        enviarMensaje(socket, linea);
    }
   }

// Devuelve 1 si es el turno del jugador que tiene ese socket, 0 si no
int esSuTurno(Partida * partida, int socket){
    // numeroJugador devuelve 1 o 2; si coincide con partida->turno, le toca a él
    return numeroJugador(partida, socket) == partida->turno;
}

// Le manda un mensaje al jugador al que le toca jugar ahora
void avisarTurno(Partida * partida){
    char texto[MAX_MSG];
    int socketTurno;

    // Averiguamos el socket del jugador que tiene el turno
    if(partida->turno == 1)
        socketTurno = partida->jugador1;
    else
        socketTurno = partida->jugador2;

    // numTurnos cuenta los turnos ya completados, así que el actual es numTurnos + 1
    sprintf(texto, "+Ok. Es tu turno. Turno %d de %d", partida->numTurnos + 1, MAX_TURNOS);
    enviarMensaje(socketTurno, texto);
}

// Prepara el control de turnos de una partida nueva:
// decide al azar quién empieza y avisa a los dos jugadores
void iniciarTurnos(Partida * partida){
    // Al empezar no se ha completado ningún turno
    partida->numTurnos = 0;

    // rand() % 2 da 0 o 1; sumándole 1 queda 1 o 2, es decir, el jugador que empieza
    partida->turno = rand() % 2 + 1;

    // Al que empieza le avisamos de que es su turno
    avisarTurno(partida);

    // Al otro le decimos que empieza su oponente
    if(partida->turno == 1)
        enviarMensaje(partida->jugador2, "+Ok. Empieza tu oponente");
    else
        enviarMensaje(partida->jugador1, "+Ok. Empieza tu oponente");
}



// Convierte un territorio escrito como texto (por ejemplo "B3") en fila y columna del tablero.
// "B3" -> fila 1, columna 2 (la letra es la fila y el número la columna, y los dos empiezan en 0 en el array)
// Devuelve 1 si el territorio existe y 0 si no. Si no existe, no toca fila ni columna
int convertirTerritorio(const char * texto, int * fila, int * columna){
    // Debe tener exactamente 2 caracteres: una letra y un número
    // (así rechazamos "", "B", "B33", "B3x"...)
    if(strlen(texto) != 2){
        return 0;
    }

    // La letra indica la fila: 'A' es la fila 0, 'B' la 1, etc.
    // Comprobamos que esté entre 'A' y la última fila del tablero (con FILAS = 5 es la 'E')
    if(texto[0] < 'A' || texto[0] >= 'A' + FILAS){
        return 0;
    }

    // El número indica la columna: '1' es la columna 0, '2' la 1, etc.
    // Comprobamos que esté entre '1' y la última columna del tablero (con COLUMNAS = 5 es el '5')
    if(texto[1] < '1' || texto[1] >= '1' + COLUMNAS){
        return 0;
    }

    // Restando el carácter base obtenemos el índice del array: 'B' - 'A' = 1 y '3' - '1' = 2
    *fila = texto[0] - 'A';
    *columna = texto[1] - '1';

    return 1;
}

// Devuelve 1 si los territorios (f1,c1) y (f2,c2) son adyacentes, es decir, si están
// inmediatamente arriba, abajo, a la izquierda o a la derecha. Las diagonales NO son adyacentes
int sonAdyacentes(int f1, int c1, int f2, int c2){
    // abs() es el valor absoluto. Sumamos cuántas filas y cuántas columnas los separan:
    //   - Arriba/abajo:        1 fila  + 0 columnas = 1  -> adyacentes
    //   - Izquierda/derecha:   0 filas + 1 columna  = 1  -> adyacentes
    //   - Diagonal:            1 fila  + 1 columna  = 2  -> NO adyacentes
    //   - El mismo territorio: 0 filas + 0 columnas = 0  -> NO adyacentes
    //   - Más lejos:           suma mayor que 1          -> NO adyacentes
    // Por tanto son adyacentes únicamente cuando la suma vale exactamente 1
    return abs(f1 - f2) + abs(c1 - c2) == 1;
}

void liberarSiVacio(Partida *partida, int fila, int columna){
    if(partida->tablero[fila][columna].unidades == 0){
        partida->tablero[fila][columna].propietario =0;
    }
}

int contarTerritorios(Partida *partida, int jugador)
{
    int contador = 0;

    for (int fila = 0; fila < FILAS; fila++) {
        for (int columna = 0; columna < COLUMNAS; columna++) {
            if (partida->tablero[fila][columna].propietario == jugador)
                contador++;
        }
    }

    return contador;
}

void finalizarPartida(Partida *partida,
                      Cliente arrayClientes[],
                      int numClientes)
{
    int territorios1 = 0;
    int territorios2 = 0;
    int unidades1 = 0;
    int unidades2 = 0;
    int ganador = 0;
    int pos1;
    int pos2;
    char mensaje[MAX_MSG];

    for (int fila = 0; fila < FILAS; fila++) {
        for (int columna = 0; columna < COLUMNAS; columna++) {
            Territorio *territorio =
                &partida->tablero[fila][columna];

            if (territorio->propietario == 1) {
                territorios1++;
                unidades1 += territorio->unidades;
            }
            else if (territorio->propietario == 2) {
                territorios2++;
                unidades2 += territorio->unidades;
            }
        }
    }

    /*
     * Primero gana quien controla más territorios.
     * Si hay empate, se comparan las unidades.
     */
    if (territorios1 > territorios2)
        ganador = 1;
    else if (territorios2 > territorios1)
        ganador = 2;
    else if (unidades1 > unidades2)
        ganador = 1;
    else if (unidades2 > unidades1)
        ganador = 2;

    pos1 = buscarCliente(partida->jugador1,
                         arrayClientes,
                         numClientes);

    pos2 = buscarCliente(partida->jugador2,
                         arrayClientes,
                         numClientes);

    if (ganador == 0) {
        snprintf(mensaje, sizeof(mensaje),
                 "+Ok. Fin de la partida: empate. J1 territorios=%d, J2 territorios=%d.",
                 territorios1, territorios2);

        if (pos1 != -1)
            enviarMensaje(arrayClientes[pos1].socket, mensaje);

        if (pos2 != -1)
            enviarMensaje(arrayClientes[pos2].socket, mensaje);
    }
    else {
        if (pos1 != -1) {
            if (ganador == 1) {
                snprintf(mensaje, sizeof(mensaje),
                         "+Ok. Fin de la partida: has ganado. Territorios=%d, unidades=%d.",
                         territorios1, unidades1);
            }
            else {
                snprintf(mensaje, sizeof(mensaje),
                         "+Ok. Fin de la partida: has perdido. Territorios=%d, unidades=%d.",
                         territorios1, unidades1);
            }

            enviarMensaje(arrayClientes[pos1].socket, mensaje);
        }

        if (pos2 != -1) {
            if (ganador == 2) {
                snprintf(mensaje, sizeof(mensaje),
                         "+Ok. Fin de la partida: has ganado. Territorios=%d, unidades=%d.",
                         territorios2, unidades2);
            }
            else {
                snprintf(mensaje, sizeof(mensaje),
                         "+Ok. Fin de la partida: has perdido. Territorios=%d, unidades=%d.",
                         territorios2, unidades2);
            }

            enviarMensaje(arrayClientes[pos2].socket, mensaje);
        }
    }

    /*
     * Los jugadores siguen conectados, pero ya no están jugando.
     */
    if (pos1 != -1) {
        arrayClientes[pos1].estado = EST_VALIDADO;
        arrayClientes[pos1].partida = -1;
    }

    if (pos2 != -1) {
        arrayClientes[pos2].estado = EST_VALIDADO;
        arrayClientes[pos2].partida = -1;
    }

    partida->activa = 0;
    partida->jugador1 = -1;
    partida->jugador2 = -1;
    partida->turno = 0;
    partida->numTurnos = 0;
}

void cambiarTurno(Partida *partida,
                  Cliente arrayClientes[],
                  int numClientes)
{
    int territorios1;
    int territorios2;

    partida->numTurnos++;

    territorios1 = contarTerritorios(partida, 1);
    territorios2 = contarTerritorios(partida, 2);

    /*
     * La partida termina si:
     * - se alcanzan 10 turnos;
     * - un jugador se queda sin territorios.
     */
    if (partida->numTurnos >= MAX_TURNOS ||
        territorios1 == 0 ||
        territorios2 == 0) {

        finalizarPartida(partida, arrayClientes, numClientes);
        return;
    }

    if (partida->turno == 1)
        partida->turno = 2;
    else
        partida->turno = 1;

    avisarTurno(partida);
}