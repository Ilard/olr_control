/*
 * ____                     _      ______ _____    _____
  / __ \                   | |    |  ____|  __ \  |  __ \
 | |  | |_ __   ___ _ __   | |    | |__  | |  | | | |__) |__ _  ___ ___
 | |  | | '_ \ / _ \ '_ \  | |    |  __| | |  | | |  _  // _` |/ __/ _ \
 | |__| | |_) |  __/ | | | | |____| |____| |__| | | | \ \ (_| | (_|  __/
  \____/| .__/ \___|_| |_| |______|______|_____/  |_|  \_\__,_|\___\___|
        | |
        |_|
 Open LED Race
 An minimalist cars race for LED strip

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.


 First public version by:
    Angel Maldonado (https://gitlab.com/angeljmc) 
    Gerardo Barbarov (gbarbarov AT singulardevices DOT com)  

  Basen on original idea and 2 players code by: 
    Gerardo Barbarov  for Arduino day Seville 2019
    https://github.com/gbarbarov/led-race

    
 Public Repository for this code:
   https://gitlab.com/open-led-race/olr-arduino

*/

 
// 2020/07/29 - Ver 0.9.d
//   --see changelog.txt
char const version[] = "0.9.d";



#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "AsyncSerialLib.h"
#include "olr-lib.h"
#include "olr-param.h"

#define PIN_LED        2  // R 500 ohms to DI pin for WS2812 and WS2813, for WS2813 BI pin of first LED to GND  ,  CAP 1000 uF to VCC 5v/GND,power supplie 5V 2A
#define PIN_AUDIO      3   // through CAP 2uf to speaker 8 ohms
#define EOL            '\n'


#define COLOR1         track.Color(255,0,0)
#define COLOR2         track.Color(0,255,0)
#define COLOR3         track.Color(0,0,255)
#define COLOR4         track.Color(255,255,255)

#define COLOR_RAMP     track.Color(64,0,64)
#define COLOR_COIN     track.Color(0,255,255)
#define COLOR_BOXMARKS track.Color(64,64,0)
#define LED_SEMAPHORE  12 


enum{
  MAX_CARS = 4,
};



enum loglevel{
    ECHO = 0,
    DISABLE = 0,
    DEBUG,
    WARNING,
    ERROR
};

enum resp{
  NOK   = -1,
  NOTHING = 0,
  OK      = 1
};

typedef struct ack{
  enum resp rp;
  char type;
}ack_t;



struct cfgcircuit{
    int outtunnel;
};

enum phases{
    IDLE = 0,
    CONFIG,
    CONFIG_OK,
    READY,
    COUNTDOWN,
    RACING,
    PAUSE,
    RESUME,
    COMPLETE,
    RACE_PHASES
};

struct race{
    struct cfgrace    cfg;
    struct cfgcircuit circ;
    bool              newcfg;
    enum phases       phase;
    byte              numcars;
    int               winner; 
};

byte SMOTOR=0;
int TBEEP=0;
int FBEEP=0;

/*------------------------------------------------------*/
enum loglevel verbose = DISABLE;

static struct race race;
static car_t cars[ MAX_CARS ];
static controller_t switchs[ MAX_CARS ];
static track_t tck;

static int const eeadrInfo = 0; 

char cmd[32];
char txbuff[64];
int const dataLength = 32;
byte data[dataLength];

static unsigned long lastmillis = 0;

int win_music[] = {
  2637, 2637, 0, 2637,
  0, 2093, 2637, 0,
  3136
};

//int TBEEP=3;

char tracksID[ NUM_TRACKS ][2] ={"U","M","B","I","O"};

/*  ----------- Function prototypes ------------------- */

void sendresponse( ack_t *ack);
ack_t parseCommands(AsyncSerial &serial);
void printdebug( const char * msg, int errlevel );
void print_cars_positions( car_t* cars);
void run_racecycle( void );
void draw_winner( track_t* tck, uint32_t color);


AsyncSerial asyncSerial(data, dataLength,
	[](AsyncSerial& sender) { ack_t ack = parseCommands( sender ); sendresponse( &ack ); }
);

//Adafruit_NeoPixel track = Adafruit_NeoPixel( MAXLED, PIN_LED, NEO_GRB + NEO_KHZ800 );
Adafruit_NeoPixel track;


char tmpmsg [20];

void setup() {

  Serial.begin(115200);
  randomSeed( analogRead(A6) + analogRead(A7) );
  controller_setup( );
  param_load( &tck.cfg );

  track = Adafruit_NeoPixel( tck.cfg.track.nled_total, PIN_LED, NEO_GRB + NEO_KHZ800 );

  controller_init( &switchs[0], DIGITAL_MODE, DIG_CONTROL_1 );
  car_init( &cars[0], &switchs[0], COLOR1 );
  controller_init( &switchs[1], DIGITAL_MODE, DIG_CONTROL_2 );
  car_init( &cars[1], &switchs[1], COLOR2 );

  race.numcars = 2;

  if( controller_isActive( DIG_CONTROL_3 )) {
    controller_init( &switchs[2], DIGITAL_MODE, DIG_CONTROL_3 );
    car_init( &cars[2], &switchs[2], COLOR3 );
    ++race.numcars;
  }

  if( controller_isActive( DIG_CONTROL_4 )) {
    controller_init( &switchs[3], DIGITAL_MODE, DIG_CONTROL_4 );
    car_init( &cars[3], &switchs[3], COLOR4 );
    ++race.numcars;
  }

  track.begin();

  // Check Box before Physic/Sound to allow user to have Box and Physics with no sound
  if ( digitalRead( DIG_CONTROL_2 ) == 0 ) { //push switch 2 on reset for activate boxes (pit lane)
    box_init( &tck );
    //box_configure( &tck, 240 );
    //box_configure( &tck, MAXLED - BOXLEN );
    track_configure( &tck, tck.cfg.track.nled_total - tck.cfg.track.box_len );
  } else{
    track_configure( &tck, 0 );
  }

  if ( digitalRead( DIG_CONTROL_1 ) == 0 ) { //push switch 1 on reset for activate physics
    ramp_init( &tck );    
    draw_ramp( &tck );
    track.show();
    delay(2000);
    if ( digitalRead( DIG_CONTROL_1 ) == 0 ) { //retain push switch  on reset for activate FX sound
                                              SMOTOR=1;
                                              tone(PIN_AUDIO,100);}
  }


  race.cfg.startline  = tck.cfg.race.startline;// true;
  race.cfg.nlap       = tck.cfg.race.nlap;// NUMLAP; 
  race.cfg.nrepeat    = tck.cfg.race.nrepeat;// 1;
  race.cfg.finishline = tck.cfg.race.finishline;// true;

  race.phase = READY;
}


void loop() {

    asyncSerial.AsyncRecieve();
    
    if ( race.phase == CONFIG ) {
      if( race.newcfg ) {
        race.newcfg = false;
        race.phase = READY;
        send_phase( race.phase );
      }
    }
    else if ( race.phase == READY ) {

      for( int i = 0; i < race.numcars; ++i) {
        car_resetPosition( &cars[i] );  
        cars[i].repeats = 0;
      }
      tck.ledcoin  = COIN_RESET;
      race.phase = COUNTDOWN;
      send_phase( race.phase );
    }
    else if( race.phase == COUNTDOWN ) {
      strip_clear( &tck );
      if( ramp_isactive( &tck ) ){
        draw_ramp( &tck );
      }
      if( box_isactive( &tck ) ) {
        draw_box_entrypoint( &tck );
      }
      track.show();
      delay( 2000 );
      
      if( race.cfg.startline ){
        start_race( );
        
        for( int i = 0; i < race.numcars; ++i ) {
          cars[i].st = CAR_ENTER;
        }
        race.phase = RACING;
        send_phase( race.phase );
      }
    }
    else if( race.phase == RACING ) {
      
      strip_clear( &tck ); 

      if( box_isactive( &tck ) ) {
        if( tck.ledcoin == COIN_RESET ) {
          tck.ledcoin = COIN_WAIT;
          tck.ledtime = millis() + random(2000,7000);
        }
        if( tck.ledcoin > 0 )
          draw_coin( &tck );
        else if( millis() > tck.ledtime )
          tck.ledcoin = random( 20, tck.cfg.track.nled_aux - 20 );
      }

      if( ramp_isactive( &tck ) )
        draw_ramp( &tck );
        
      for( int i = 0; i < race.numcars; ++i ) {
        run_racecycle( &cars[i], i );
        if( cars[i].st == CAR_FINISH ) {
          race.phase = COMPLETE;
          race.winner = i;
          send_phase( race.phase );
          break;
        }
      }

      track.show();
      if (SMOTOR==1) tone(PIN_AUDIO,FBEEP+int(cars[0].speed*440*1)+int(cars[1].speed*440*2)+int(cars[2].speed*440*3)+int(cars[3].speed*440*4));
      if (TBEEP>0) {TBEEP--;} else {FBEEP=0;};

    /* Print p command!!! */
      unsigned long nowmillis = millis();
      if( abs( nowmillis - lastmillis ) > 100 ){
        lastmillis = nowmillis;
        print_cars_positions( cars );
      }
    /* ---------------- */
    }
    else if( race.phase == COMPLETE ) {
      strip_clear( &tck );
      track.show();
      if ( race.cfg.finishline ){
        draw_winner( &tck, cars[race.winner].color );
        sound_winner( &tck, race.winner );
        strip_clear( &tck );
      }
      track.show();
      race.phase = READY;
    }

}

void send_phase( int phase ) {
    Serial.print( "R" );
    Serial.print( phase );
    Serial.print( EOL );
}


void run_racecycle( car_t *car, int i ) {
    struct cfgtrack const* cfg = &tck.cfg.track;
    
    if( car->st == CAR_ENTER ) {
        car_resetPosition( car );
        if( car->repeats < race.cfg.nrepeat )
          car->st = CAR_RACING;
        else
          car->st = CAR_GO_OUT;
    }
    
    if( car->st == CAR_RACING ) {
        update_track( &tck, car );
        car_updateController( car );
        draw_car( &tck, car );

        if( car->nlap == race.cfg.nlap 
              && !car->leaving
              && car->dist > ( cfg->nled_main*car->nlap - race.circ.outtunnel ) ) {
            car->leaving = true;
            car->st = CAR_LEAVING;
        }

        if( car->nlap > race.cfg.nlap ) {
            ++car->repeats;
            car->st = CAR_GO_OUT;
        }

        if( car->repeats >= race.cfg.nrepeat 
              && race.cfg.finishline ) {
            car->st = CAR_FINISH;  
        }
    }

    if ( car->st == CAR_FINISH ){
        car->trackID = NOT_TRACK;
        sprintf( txbuff, "w%d%c", i + 1, EOL );
        Serial.print( txbuff );
        car_resetPosition( car );
    }
}


int get_relative_position( car_t* car ) {
    enum{
      MIN_RPOS = 0,
      MAX_RPOS = 99,
    };
    struct cfgtrack const* cfg = &tck.cfg.track;
    int trackdist = 0;
    int pos = 0;

    switch ( car->trackID ){
      case TRACK_MAIN:
        trackdist = (int)car->dist % cfg->nled_main;
        pos = map(trackdist, 0, cfg->nled_main -1, MIN_RPOS, MAX_RPOS);
      break;
      case TRACK_AUX:
        trackdist = (int)car->dist_aux;
        pos = map(trackdist, 0, cfg->nled_aux -1, MIN_RPOS, MAX_RPOS);
      break;
    }
    return pos;
}


void print_cars_positions( car_t* cars ) {
    
    bool outallcar = true;
    for( int i = 0; i < race.numcars; ++i)
      outallcar &= cars[i].st == CAR_WAITING;
    
    if ( outallcar ) return;
    
    for( int i = 0; i < race.numcars; ++i ) {
      int const rpos = get_relative_position( &cars[i] );
      sprintf( txbuff, "p%d%s%d,%d%c", i + 1, tracksID[cars[i].trackID], cars[i].nlap, rpos, EOL );
      Serial.print( txbuff );
    }
}

void start_race( ) {
    track.setPixelColor(LED_SEMAPHORE, track.Color(255,0,0));   
    track.show();    
    tone(PIN_AUDIO,400);
    delay(2000);
    noTone(PIN_AUDIO);    
    track.setPixelColor(LED_SEMAPHORE, track.Color(0,0,0));  
    track.setPixelColor(LED_SEMAPHORE-1, track.Color(255,255,0));   
    track.show();    
    tone(PIN_AUDIO,600);
    delay(2000);
    noTone(PIN_AUDIO);    
    track.setPixelColor(LED_SEMAPHORE-1, track.Color(0,0,0)); 
    track.setPixelColor(LED_SEMAPHORE-2,  track.Color(0,255,0));
    track.show();    
    tone(PIN_AUDIO,1200);
    delay(2000);
    noTone(PIN_AUDIO); 
}


void sound_winner( track_t* tck, int winner ) {
  int const msize = sizeof(win_music) / sizeof(int);
  for (int note = 0; note < msize; note++) {
    tone(PIN_AUDIO, win_music[note],200);
    delay(230);
    noTone(PIN_AUDIO);
  }
}


void strip_clear( track_t* tck ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
    for( int i=0; i < cfg->nled_main; i++)
        track.setPixelColor( i, track.Color(0,0,0) );

    for( int i=0; i < cfg->nled_aux; i++)
        track.setPixelColor( cfg->nled_main+i, track.Color(0,0,0) );
}


void draw_coin( track_t* tck ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
    track.setPixelColor( 1 + cfg->nled_main + cfg->nled_aux - tck->ledcoin,COLOR_COIN );
}

void draw_winner( track_t* tck, uint32_t color) {
  struct cfgtrack const* cfg = &tck->cfg.track;
  for(int i=16; i < cfg->nled_main; i=i+(8 * cfg->nled_main / 300 )){
      track.setPixelColor( i , color );
      track.setPixelColor( i-16 ,0 );
      track.show();
  }
}

void draw_car( track_t* tck, car_t* car ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
    
    switch ( car->trackID ){
      case TRACK_MAIN:
        for(int i=0; i<= car->nlap; ++i )
          track.setPixelColor( ((word)car->dist % cfg->nled_main) + i, car->color );
      break;
      case TRACK_AUX:
        for(int i=0; i<= car->nlap; ++i )     
          track.setPixelColor( (word)(cfg->nled_main + cfg->nled_aux - car->dist_aux) + i, car->color);         
      break;
    }
}

void draw_ramp( track_t* tck ) {
    struct cfgramp const* r = &tck->cfg.ramp;
    byte dist = 0;
    byte intensity = 0;
    for( int i = r->init; i <= r->center; ++i ) {
      dist = r->center - r->init;
      intensity = ( 32 * (i - r->init) ) / dist;
      track.setPixelColor( i, track.Color( intensity,0,intensity ) );
    }
    for( int i = r->center; i <= r->end; ++i ) {
      dist = r->end - r->center;
      intensity = ( 32 * ( r->end - i ) ) / dist;
      track.setPixelColor( i, track.Color( intensity,0,intensity ) );
    }
}


void draw_box_entrypoint( track_t* tck ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
    track.setPixelColor(cfg->init_aux ,COLOR_BOXMARKS ); // Pit lane exit (race start)
    track.setPixelColor(cfg->init_aux - cfg->nled_aux + 1  ,COLOR_BOXMARKS ); // Pit lane Entrance
}




void printdebug( const char * msg, int errlevel ) {

  char header[4];
  sprintf( header, "!%d,", errlevel );
  Serial.print( header );
  Serial.print( msg );
  Serial.print( EOL );
}


ack_t parseCommands(AsyncSerial &serial) {

  ack_t ack = { .rp = NOK, .type = '\0' };
  memcpy(&cmd, serial.GetContent(), serial.GetContentLength());

  if ( ECHO )  //VERBOSE ECHO
    printdebug( cmd, DEBUG );

  if( cmd[0] == '#' ) {
    ack.type = cmd[0];
    Serial.print("#");
    Serial.print( EOL );
    ack.rp = NOTHING;
  }
  else if( cmd[0] == 'R' ) {
    ack.type = cmd[0];
    int const phase = atoi( cmd + 1);
    if( 0 > phase || RACE_PHASES <= phase) return ack;
    race.phase = (enum phases) phase;
    ack.rp = OK;

    if ( verbose >= DEBUG ) { //VERBOSE
      sprintf( txbuff, "%s %d", "RACE PHASE: ", race.phase);
      printdebug( txbuff, DEBUG );
    }
  }
  else if( cmd[0] == 'C' ) { //Parse race configuration -> C1.2.3.0
    ack.type = cmd[0];

    char * pch = strtok (cmd,"C");
    if( !pch ) return ack;

    pch = strtok (pch, "," );
    if( !pch ) return ack;
    //cfg.startline = atoi( pch );
    int startline = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    //cfg.nlap = atoi( pch );
    int nlap = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    //cfg.nrepeat = atoi( pch );
    int nrepeat = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    //cfg.finishline = atoi( pch );
    int finishline = atoi( pch );

    int err = race_configure( &tck, startline, nlap, nrepeat, finishline);
    if( err ) return ack;
    EEPROM.put( eeadrInfo, tck.cfg );

    race.cfg.startline  = tck.cfg.race.startline;
    race.cfg.nlap       = tck.cfg.race.nlap;
    race.cfg.nrepeat    = tck.cfg.race.nrepeat;
    race.cfg.finishline = tck.cfg.race.finishline;
    
    race.newcfg = true;
    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      sprintf( txbuff, "%s %d, %d, %d, %d", "RACE CONFIG: ",
                                    race.cfg.startline,
                                    race.cfg.nlap,
                                    race.cfg.nrepeat,
                                    race.cfg.finishline );
      printdebug( txbuff, DEBUG );
    }
  }
  else if( cmd[0] == 'T' ) { //Parse Track configuration -> Track length
    ack.type = cmd[0];

    char * pch = strtok (cmd,"T");
    if( !pch ) return ack;

    int nled = atoi( cmd + 1 );
    int err = tracklen_configure( &tck, nled);
    if( err ) return ack;
    track_configure( &tck, 0);
    if( err ) return ack;
    
    EEPROM.put( eeadrInfo, tck.cfg );

    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      struct cfgtrack const* cfg = &tck.cfg.track;
      sprintf( txbuff, "%s %d, %d, %d, %d, %d", "TRACK CONFIG: ",
                                    cfg->nled_total,
                                    cfg->nled_main,
                                    cfg->nled_aux,
                                    cfg->init_aux,
                                    cfg->box_len);
      printdebug( txbuff, DEBUG );
    }

  }
  else if( cmd[0] == 'B' ) { //Parse BoxLenght Configuration 
    ack.type = cmd[0];

    char * pch = strtok (cmd,"B");
    if( !pch ) return ack;

    int boxlen = atoi( cmd + 1 );
    int err = boxlen_configure( &tck, boxlen);
    if( err ) return ack;
    
    EEPROM.put( eeadrInfo, tck.cfg );

    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      struct cfgtrack const* cfg = &tck.cfg.track;
      sprintf( txbuff, "%s %d, %d, %d, %d, %d", "TRACK CONFIG: ",
                                    cfg->nled_total,
                                    cfg->nled_main,
                                    cfg->nled_aux,
                                    cfg->init_aux,
                                    cfg->box_len);
      printdebug( txbuff, DEBUG );
    }

  }
  else if( cmd[0] == 'A' ) { // Parse Ramp configuration -> A<center>.<high> 
    ack.type = cmd[0];

    char * pch = strtok (cmd,"A");
    if( !pch ) return ack;

    pch = strtok (pch, "," );
    if( !pch ) return ack;
    int init = atoi( pch );

    pch = strtok (NULL, "," );
    if( !pch ) return ack;
    int center = atoi( pch );

    pch = strtok (NULL, "," );
    if( !pch ) return ack;
    int end = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    int high = atoi( pch );

    int err = ramp_configure( &tck, init, center, end, high );
    if( err ) return ack;
    EEPROM.put( eeadrInfo, tck.cfg );
    
    ramp_init( &tck );
    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      struct cfgramp const* cfg = &tck.cfg.ramp;
      sprintf( txbuff, "%s %d, %d, %d, %d", "RAMP CONFIG: ",
                                    cfg->init,
                                    cfg->center,
                                    cfg->end,
                                    cfg->high );
      printdebug( txbuff, DEBUG );
    }
  }
  
  else if( cmd[0] == 'D') {
    ack.type = cmd[0]; 
    param_setdefault( &tck.cfg );
    EEPROM.put( eeadrInfo, tck.cfg );
    sprintf( txbuff, "%s", "Load default" );
    printdebug( txbuff, DEBUG );
    ack.rp = OK;
  }
  else if( cmd[0] == ':' ) { // Set board Unique Id
    struct brdinfo* info = &tck.cfg.info;
    ack.type = cmd[0];
    if( strlen(cmd + 1) > LEN_UID ) return ack;
    strcpy( info->uid, cmd + 1 );
    EEPROM.put( eeadrInfo, tck.cfg );
    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      sprintf( txbuff, "%s %s", "UID: ", tck.cfg.info.uid );
      printdebug( txbuff, DEBUG );
    }
  }
  else if( cmd[0] == '$' ) { // Get Board UID
    sprintf( txbuff, "%s%s%c", "$", tck.cfg.info.uid, EOL );
    Serial.print( txbuff );
    ack.rp = NOTHING;
  }
  else if( cmd[0] == '%' ) { // Get Software Version
    sprintf( txbuff, "%s%s%c", "%", version, EOL );
    Serial.print( txbuff );
    ack.rp = NOTHING;
  }
  else if( cmd[0] == 'Q' ) { // Get configuration Info
    struct cfgparam const* cfg = &tck.cfg;
    sprintf( txbuff, "%s:%d,%d,%d,%d,%d%c", "TRACK",
                                    cfg->track.nled_total,
                                    cfg->track.nled_main,
                                    cfg->track.nled_aux,
                                    cfg->track.init_aux,
                                    cfg->track.box_len,
                                    EOL );
    Serial.print( txbuff );
    sprintf( txbuff, "%s:%d,%d,%d,%d%c", "RAMP",
                                    cfg->ramp.init,
                                    cfg->ramp.center,
                                    cfg->ramp.end,
                                    cfg->ramp.high,
                                    EOL );
    Serial.print( txbuff );
    sprintf( txbuff, "%s:%d,%d,%d,%d%c", "RACE",
                                    cfg->race.startline,
                                    cfg->race.nlap,
                                    cfg->race.nrepeat,
                                    cfg->race.finishline,
                                    EOL );
    Serial.print( txbuff );

    ack.rp = NOTHING;
  }

  return ack;
}

void sendresponse( ack_t *ack) {

  if( ack->rp == OK ) { 
    Serial.print( ack->type );
    Serial.print("OK");
    Serial.print( EOL );
  }
  else if( ack->rp == NOK ) {
    Serial.print( ack->type );
    Serial.print("NOK");
    Serial.print( EOL );
  }
  memset( &cmd, '\0' , sizeof(cmd) );
}


 
void param_load( struct cfgparam* cfg ) {
    int cfgversion;
    int eeAdress = eeadrInfo;
    EEPROM.get( eeAdress, tck.cfg );
    eeAdress += sizeof( cfgparam );
    EEPROM.get( eeAdress, cfgversion );

    sprintf( txbuff, "%s:%d%c", "Parameters Loaded from EEPROM Ver:", cfgversion, EOL );
    Serial.print( txbuff );
    

    if ( cfgversion != CFG_VER ) {
      param_setdefault( &tck.cfg );
      eeAdress = 0;
      EEPROM.put( eeAdress, tck.cfg );
      eeAdress += sizeof( cfgparam );
      EEPROM.put( eeAdress, CFG_VER );
      Serial.print("DEFAULT PAREMETRS LOADED (and Stored in EEPROM)\n");
    }
}
