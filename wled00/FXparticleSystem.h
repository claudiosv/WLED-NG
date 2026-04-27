/*
  FXparticleSystem.cpp

  Particle system with functions for particle generation, particle movement and particle rendering to RGB matrix.
  by DedeHai (Damian Schneider) 2013-2024

  Copyright (c) 2024  Damian Schneider
  Licensed under the EUPL v. 1.2 or later
*/

#ifdef WLED_DISABLE_2D
#define WLED_DISABLE_PARTICLESYSTEM2D
#endif

#if !(defined(WLED_DISABLE_PARTICLESYSTEM2D) && defined(WLED_DISABLE_PARTICLESYSTEM1D))  // not both disabled

#include <stdint.h>

#include "wled.h"

#define PS_P_MAXSPEED \
  120  // maximum speed a particle can have (vx/vy is int8), limiting below 127 to avoid overflows in collisions due to
       // rounding errors
enum {
MAX_MEMIDLE = \
  10  // max idle time (in frames) before memory is deallocated (if deallocated during an effect, it will crash!)
};

// #define WLED_DEBUG_PS // note: enabling debug uses ~3k of flash

#ifdef WLED_DEBUG_PS
#define PSPRINT(x)   Serial.print(x)
#define PSPRINTLN(x) Serial.println(x)
#else
#define PSPRINT(x)
#define PSPRINTLN(x)
#endif

// limit speed of particles (used in 1D and 2D)
static inline int32_t limitSpeed(const int32_t speed) {
  return speed > PS_P_MAXSPEED
             ? PS_P_MAXSPEED
             : (speed < -PS_P_MAXSPEED
                    ? -PS_P_MAXSPEED
                    : speed);  // note: this is slightly faster than using min/max at the cost of 50bytes of flash
}
#endif

#ifndef WLED_DISABLE_PARTICLESYSTEM2D
// memory allocation (based on reasonable segment size and available FX memory)
#if ARDUINO_ARCH_ESP32S2
#define MAXPARTICLES_2D       1024
#define MAXSOURCES_2D         64
#define SOURCEREDUCTIONFACTOR 6
#else
#define MAXPARTICLES_2D       2048
#define MAXSOURCES_2D         128
#define SOURCEREDUCTIONFACTOR 4
#endif

// particle dimensions (subpixel division)
enum {
PS_P_RADIUS =     64  // subpixel size, each pixel is divided by this for particle movement (must be a power of 2)
};
#define PS_P_HALFRADIUS (PS_P_RADIUS >> 1)
enum {
PS_P_RADIUS_SHIFT =  6,   // shift for RADIUS
PS_P_SURFACE =       12,  // shift: 2^PS_P_SURFACE = (PS_P_RADIUS)^2
PS_P_MINHARDRADIUS = 64,  // minimum hard surface radius for collisions
PS_P_MINSURFACEHARDNESS = \
  128  // minimum hardness used in collision impulse calculation, below this hardness, particles become sticky
};

// struct for PS settings (shared for 1D and 2D class)
using PSsettings2D = union {
  struct {  // one byte bit field for 2D settings
    bool wrapX           : 1;
    bool wrapY           : 1;
    bool bounceX         : 1;
    bool bounceY         : 1;
    bool killoutofbounds : 1;  // if set, out of bound particles are killed immediately
    bool useGravity      : 1;  // set to 1 if gravity is used, disables bounceY at the top
    bool useCollisions   : 1;
    bool colorByAge      : 1;  // if set, particle hue is set by ttl value in render function
  };
  byte asByte;  // access as a byte, order is: LSB is first entry in the list above
};

// struct for a single particle
using PSparticle = struct {  // 10 bytes
  int16_t  x;     // x position in particle system
  int16_t  y;     // y position in particle system
  uint16_t ttl;   // time to live in frames
  int8_t   vx;    // horizontal velocity
  int8_t   vy;    // vertical velocity
  uint8_t  hue;   // color hue
  uint8_t  sat;   // particle color saturation
};

// struct for particle flags note: this is separate from the particle struct to save memory (ram alignment)
using PSparticleFlags = union {
  struct {                 // 1 byte
    bool outofbounds : 1;  // out of bounds flag, set to true if particle is outside of display area
    bool collide     : 1;  // if set, particle takes part in collisions
    bool perpetual   : 1;  // if set, particle does not age (TTL is not decremented in move function, it still dies from
                           // killoutofbounds)
    bool custom1 : 1;      // unused custom flags, can be used by FX to track particle states
    bool custom2 : 1;
    bool custom3 : 1;
    bool custom4 : 1;
    bool custom5 : 1;
  };
  byte asByte;  // access as a byte, order is: LSB is first entry in the list above
};

// struct for additional particle settings (option)
using PSadvancedParticle = struct {         // 2 bytes
  uint8_t size;          // particle size, 255 means 10 pixels in diameter, set perParticleSize = true to enable
  uint8_t forcecounter;  // counter for applying forces to individual particles
};

// struct for advanced particle size control (option)
using PSsizeControl = struct {              // 8 bytes
  uint8_t asymmetry;          // asymmetrical size (0=symmetrical, 255 fully asymmetric)
  uint8_t asymdir;            // direction of asymmetry, 64 is x, 192 is y (0 and 128 is symmetrical)
  uint8_t maxsize;            // target size for growing
  uint8_t minsize;            // target size for shrinking
  uint8_t sizecounter   : 4;  // counters used for size contol (grow/shrink/wobble)
  uint8_t wobblecounter : 4;
  uint8_t growspeed     : 4;
  uint8_t shrinkspeed   : 4;
  uint8_t wobblespeed   : 4;
  bool    grow          : 1;  // flags
  bool    shrink        : 1;
  bool    pulsate       : 1;  // grows & shrinks & grows & ...
  bool    wobble        : 1;  // alternate x and y size
};

// struct for a particle source (20 bytes)
using PSsource = struct {
  uint16_t        minLife;      // minimum ttl of emittet particles
  uint16_t        maxLife;      // maximum ttl of emitted particles
  PSparticle      source;       // use a particle as the emitter source (speed, position, color)
  PSparticleFlags sourceFlags;  // flags for the source particle
  int8_t          var;          // variation of emitted speed (adds random(+/- var) to speed)
  int8_t          vx;           // emitting speed
  int8_t          vy;
  uint8_t         size;  // particle size (advanced property), global size is added on top to this size
};

// class uses approximately 60 bytes
class ParticleSystem2D {
 public:
  ParticleSystem2D(uint32_t width, uint32_t height, uint32_t numberofparticles,
                   uint32_t numberofsources, bool isadvanced = false,
                   bool sizecontrol = false);  // constructor
  // note: memory is allcated in the FX function, no deconstructor needed
  void update();                         // update the particles according to set options and render to the matrix
  void updateFire(uint8_t intensity);  // update function for fire
  void updateSystem();                   // call at the beginning of every FX, updates pointers and dimensions
  void particleMoveUpdate(PSparticle &part, PSparticleFlags &partFlags, PSsettings2D *options = nullptr,
                          PSadvancedParticle *advancedproperties = nullptr);  // move function
  // particle emitters
  int32_t sprayEmit(const PSsource &emitter);
  void    flameEmit(const PSsource &emitter);
  int32_t angleEmit(PSsource &emitter, uint16_t angle, int32_t speed);
  // particle physics
  void              applyGravity(PSparticle &part);  // applies gravity to single particle (use this for sources)
  static [[gnu::hot]] void applyForce(PSparticle &part, int8_t xforce, int8_t yforce, uint8_t &counter);
  [[gnu::hot]] void applyForce(uint32_t particleindex, int8_t xforce,
                               int8_t yforce);                       // use this for advanced property particles
  void              applyForce(int8_t xforce, int8_t yforce);  // apply a force to all particles
  void              applyAngleForce(PSparticle &part, int8_t force, uint16_t angle, uint8_t &counter);
  void              applyAngleForce(uint32_t particleindex, int8_t force,
                                    uint16_t angle);  // use this for advanced property particles
  void              applyAngleForce(int8_t force, uint16_t angle);   // apply angular force to all particles
  static void              applyFriction(PSparticle &part, int32_t coefficient);  // apply friction to specific particle
  void              applyFriction(int32_t coefficient) const;                    // apply friction to all used particles
  void pointAttractor(uint32_t particleindex, PSparticle &attractor, uint8_t strength, bool swallow);
  // set options  note: inlining the set function uses more flash so dont optimize
  void setUsedParticles(uint8_t percentage);    // set the percentage of particles used in the system, 255=100%
  void setCollisionHardness(uint8_t hardness);  // hardness for particle collisions (255 means full hard)
  void setWallHardness(uint8_t hardness);       // hardness for bouncing on the wall if bounceXY is set
  void setWallRoughness(uint8_t roughness);     // wall roughness randomizes wall collisions
  void setMatrixSize(uint32_t x, uint32_t y);
  void setWrapX(bool enable);
  void setWrapY(bool enable);
  void setBounceX(bool enable);
  void setBounceY(bool enable);
  void setKillOutOfBounds(bool enable);  // if enabled, particles outside of matrix instantly die
  void setSaturation(uint8_t sat);       // set global color saturation
  void setColorByAge(bool enable);
  void setMotionBlur(uint8_t bluramount);  // note: motion blur can only be used if 'particlesize' is set to zero
  void setSmearBlur(uint8_t bluramount);   // enable 2D smeared blurring of full frame
  void setParticleSize(uint8_t size);
  void setGravity(int8_t force = 8);
  void enableParticleCollisions(bool enable, uint8_t hardness = 255);

  PSparticle         *particles;      // pointer to particle array
  PSparticleFlags    *particleFlags;  // pointer to particle flags array
  PSsource           *sources;        // pointer to sources
  PSadvancedParticle *advPartProps;   // pointer to advanced particle properties (can be NULL)
  PSsizeControl      *advPartSize;    // pointer to advanced particle size control (can be NULL)
  uint8_t *PSdataEnd;  // points to first available byte after the PSmemory, is set in setPointers(). use this for FX
                       // custom data
  int32_t maxX, maxY;  // particle system size i.e. width-1 / height-1 in subpixels, Note: all "max" variables must be
                       // signed to compare to coordinates (which are signed)
  int32_t maxXpixel, maxYpixel;  // last physical pixel that can be drawn to (FX can read this to read segment size if
                                 // required), equal to width-1 / height-1
  uint32_t numSources;           // number of sources
  uint32_t usedParticles;        // number of particles used in animation, is relative to 'numParticles'
  bool     perParticleSize;  // if true, uses individual particle sizes from advPartProps if available (disabled when
                             // calling setParticleSize())
                             // note: some variables are 32bit for speed and code size at the cost of ram

 private:
  // rendering functions
  void              render();
  [[gnu::hot]] void renderParticle(uint32_t particleindex, uint8_t brightness, const CRGBW &color,
                                   bool wrapX, bool wrapY);
  void              renderLargeParticle(uint32_t size, uint32_t particleindex, uint8_t brightness,
                                        const CRGBW &color, bool wrapX, bool wrapY);
  // paricle physics applied by system if flags are set
  void applyGravity();  // applies gravity to all particles
  void handleCollisions();
  void collideParticles(PSparticle &particle1, PSparticle &particle2, int32_t dx, int32_t dy, uint32_t collDistSq,
                        int32_t massratio1, int32_t massratio2);
  void fireParticleupdate() const;
  // utility functions
  void updatePSpointers(bool isadvanced,
                        bool sizecontrol);  // update the data pointers to current segment data space
  static bool updateSize(PSadvancedParticle *advprops, PSsizeControl *advsize);  // advanced size control
  static void getParticleXYsize(PSadvancedParticle *advprops, PSsizeControl *advsize, uint32_t &xsize, uint32_t &ysize);
  [[gnu::hot]] void bounce(int8_t &incomingspeed, int8_t &parallelspeed, int32_t &position,
                           uint32_t maxposition) const;  // bounce on a wall
  // note: variables that are accessed often are 32bit for speed
  uint32_t *framebuffer_;  // frame buffer for rendering. note: using CRGBW as the buffer is slower, ESP compiler seems
                          // to optimize this better giving more consistent FPS
  PSsettings2D particlesettings_;  // settings used when updating particles (can also used by FX to move sources), do not
                                  // edit properties directly, use functions above
  uint32_t numParticles_;          // total number of particles allocated by this system
  uint32_t emitIndex_;             // index to count through particles to emit so searching for dead pixels is faster
  int32_t  collisionHardness_;
  uint32_t wallHardness_;
  uint32_t wallRoughness_;       // randomizes wall collisions
  uint32_t particleHardRadius_;  // hard surface radius of a particle, used for collision detection (32bit for speed)
  uint16_t collisionStartIdx_;   // particle array start index for collision detection
  uint8_t  fireIntesity_ = 0;    // fire intensity, used for fire mode (flash use optimization, better than passing an
                                // argument to render function)
  uint8_t forcecounter_;         // counter for globally applied forces
  uint8_t gforcecounter_;        // counter for global gravity
  int8_t  gforce_;               // gravity strength, default is 8 (negative is allowed, positive is downwards)
  // global particle properties for basic particles
  uint8_t particlesize_;  // global particle size, 0 = 1 pixel, 1 = 2 pixels, 255 = 10 pixels (note: this is also added
                         // to individual sized particles, set to 0 or 1 for standard advanced particle rendering)
  uint8_t motionBlur_;    // motion blur, values > 100 gives smoother animations. Note: motion blurring does not work if
                         // particlesize is > 0
  uint8_t smearBlur_;     // 2D smeared blurring of full frame
};

// initialization functions (not part of class)
bool     initParticleSystem2D(ParticleSystem2D *&PartSys, uint32_t requestedsources,
                              uint32_t additionalbytes = 0, bool advanced = false,
                              bool sizecontrol = false);
uint32_t calculateNumberOfParticles2D(uint32_t pixels, bool advanced, bool sizecontrol);
uint32_t calculateNumberOfSources2D(uint32_t pixels, uint32_t requestedsources);
bool     allocateParticleSystemMemory2D(uint32_t numparticles, uint32_t numsources, bool advanced,
                                        bool sizecontrol, uint32_t additionalbytes);

// distance-based brightness for ellipse rendering, returns brightness (0-255) based on distance from ellipse center
inline uint8_t calculateEllipseBrightness(int32_t dx, int32_t dy, int32_t rxsq, int32_t rysq, uint8_t maxBrightness) {
  // square the distances
  uint32_t dx_sq = dx * dx;
  uint32_t dy_sq = dy * dy;

  uint32_t dist_sq =
      ((dx_sq << 8) / rxsq) +
      ((dy_sq << 8) / rysq);  // normalized squared distance in fixed point: (dx²/rx²) * 256 + (dy²/ry²) * 256

  if (dist_sq >= 256) {
    return 0;  // pixel is outside the ellipse, unit radius in fixed point: 256 = 1.0
  }
  // if (dist_sq <= 96) return maxBrightness; // core at full brightness
  int32_t falloff = 256 - dist_sq;
  return (maxBrightness * falloff) >> 8;  // linear falloff
  // return (maxBrightness * falloff * falloff) >> 16; // squared falloff for even softer edges
}
#endif  // WLED_DISABLE_PARTICLESYSTEM2D

////////////////////////
// 1D Particle System //
////////////////////////
#ifndef WLED_DISABLE_PARTICLESYSTEM1D
// memory allocation
#if ARDUINO_ARCH_ESP32S2
#define MAXPARTICLES_1D 1300
#define MAXSOURCES_1D   32
#else
#define MAXPARTICLES_1D 2600
#define MAXSOURCES_1D   64
#endif

// particle dimensions (subpixel division)
enum {
PS_P_RADIUS_1D = \
  32  // subpixel size, each pixel is divided by this for particle movement, if this value is changed, also change the
};
      // shift defines (next two lines)
#define PS_P_HALFRADIUS_1D    (PS_P_RADIUS_1D >> 1)
enum {
PS_P_RADIUS_SHIFT_1D =  5,   // 1 << PS_P_RADIUS_SHIFT = PS_P_RADIUS
PS_P_SURFACE_1D =       5,   // shift: 2^PS_P_SURFACE = PS_P_RADIUS_1D
PS_P_MINHARDRADIUS_1D = 32,  // minimum hard surface radius note: do not change or hourglass effect will be broken
PS_P_MINSURFACEHARDNESS_1D = 120  // minimum hardness used in collision impulse calculation
};

// struct for PS settings (shared for 1D and 2D class)
using PSsettings1D = union {
  struct {
    // one byte bit field for 1D settings
    bool wrap            : 1;
    bool bounce          : 1;
    bool killoutofbounds : 1;  // if set, out of bound particles are killed immediately
    bool useGravity      : 1;  // set to 1 if gravity is used, disables bounceY at the top
    bool useCollisions   : 1;
    bool colorByAge      : 1;  // if set, particle hue is set by ttl value in render function
    bool colorByPosition : 1;  // if set, particle hue is set by its position in the strip segment
    bool unused          : 1;
  };
  byte asByte;  // access as a byte, order is: LSB is first entry in the list above
};

// struct for a single particle (8 bytes)
using PSparticle1D = struct {
  int32_t  x;    // x position in particle system
  uint16_t ttl;  // time to live in frames
  int8_t   vx;   // horizontal velocity
  uint8_t  hue;  // color hue
};

// struct for particle flags
using PSparticleFlags1D = union {
  struct {                 // 1 byte
    bool outofbounds : 1;  // out of bounds flag, set to true if particle is outside of display area
    bool collide     : 1;  // if set, particle takes part in collisions
    bool perpetual   : 1;  // if set, particle does not age (TTL is not decremented in move function, it still dies from
                           // killoutofbounds)
    bool reversegrav    : 1;  // if set, gravity is reversed on this particle
    bool forcedirection : 1;  // direction the force was applied, 1 is positive x-direction (used for collision
                              // stacking, similar to reversegrav) TODO: not used anymore, can be removed
    bool fixed   : 1;         // if set, particle does not move (and collisions make other particles revert direction),
    bool custom1 : 1;         // unused custom flags, can be used by FX to track particle states
    bool custom2 : 1;
  };
  byte asByte;  // access as a byte, order is: LSB is first entry in the list above
};

// struct for additional particle settings (optional)
using PSadvancedParticle1D = struct {
  uint8_t sat;           // color saturation
  uint8_t size;          // particle size, 255 means 10 pixels in diameter, this overrides global size setting
  uint8_t forcecounter;  // counter for applying forces to individual particles
};

// struct for a particle source (20 bytes)
using PSsource1D = struct {
  uint16_t          minLife;      // minimum ttl of emittet particles
  uint16_t          maxLife;      // maximum ttl of emitted particles
  PSparticle1D      source;       // use a particle as the emitter source (speed, position, color)
  PSparticleFlags1D sourceFlags;  // flags for the source particle
  int8_t            var;          // variation of emitted speed (adds random(+/- var) to speed)
  int8_t            v;            // emitting speed
  uint8_t           sat;          // color saturation (advanced property)
  uint8_t           size;         // particle size (advanced property)
  // note: there is 3 bytes of padding added here
};

class ParticleSystem1D {
 public:
  ParticleSystem1D(uint32_t length, uint32_t numberofparticles, uint32_t numberofsources,
                   bool isadvanced = false);  // constructor
  // note: memory is allcated in the FX function, no deconstructor needed
  void update();        // update the particles according to set options and render to the matrix
  void updateSystem();  // call at the beginning of every FX, updates pointers and dimensions
  // particle emitters
  int32_t sprayEmit(const PSsource1D &emitter);
  void    particleMoveUpdate(PSparticle1D &part, PSparticleFlags1D &partFlags, PSsettings1D *options = nullptr,
                             PSadvancedParticle1D *advancedproperties = nullptr);  // move function
  // particle physics
  static [[gnu::hot]] void applyForce(PSparticle1D &part, int8_t xforce,
                               uint8_t &counter);     // apply a force to a single particle
  void              applyForce(int8_t xforce);  // apply a force to all particles
  void applyGravity(PSparticle1D      &part,
                    PSparticleFlags1D &partFlags);  // applies gravity to single particle (use this for sources)
  void applyFriction(int32_t coefficient) const;    // apply friction to all used particles
  // set options
  void setUsedParticles(uint8_t percentage);  // set the percentage of particles used in the system, 255=100%
  void setWallHardness(uint8_t hardness);     // hardness for bouncing on the wall if bounceXY is set
  void setSize(uint32_t x);                   // set particle system size (= strip length)
  void setWrap(bool enable);
  void setBounce(bool enable);
  void setKillOutOfBounds(bool enable);  // if enabled, particles outside of matrix instantly die
                                               // void setSaturation(uint8_t sat); // set global color saturation
  void setColorByAge(bool enable);
  void setColorByPosition(bool enable);
  void setMotionBlur(uint8_t bluramount);  // note: motion blur can only be used if 'particlesize' is set to zero
  void setSmearBlur(uint8_t bluramount);   // enable 1D smeared blurring of full frame
  void setParticleSize(uint8_t size);  // particle diameter: size 0 = 1 pixel, size 1 = 2 pixels, size = 255 = 10
                                             // pixels, disables per particle size control if called
  void setGravity(int8_t force = 8);
  void enableParticleCollisions(bool enable, uint8_t hardness = 255);

  PSparticle1D         *particles;      // pointer to particle array
  PSparticleFlags1D    *particleFlags;  // pointer to particle flags array
  PSsource1D           *sources;        // pointer to sources
  PSadvancedParticle1D *advPartProps;   // pointer to advanced particle properties (can be NULL)
  // PSsizeControl *advPartSize; // pointer to advanced particle size control (can be NULL)
  uint8_t *PSdataEnd;   // points to first available byte after the PSmemory, is set in setPointers(). use this for FX
                        // custom data
  int32_t maxX;         // particle system size i.e. width-1, Note: all "max" variables must be signed to compare to
                        // coordinates (which are signed)
  int32_t maxXpixel;    // last physical pixel that can be drawn to (FX can read this to read segment size if required),
                        // equal to width-1
  uint32_t numSources;  // number of sources
  uint32_t usedParticles;    // number of particles used in animation, is relative to 'numParticles'
  bool     perParticleSize;  // if true, uses individual particle sizes from advPartProps if available (disabled when
                             // calling setParticleSize())

 private:
  // rendering functions
  void render();
  void renderParticle(uint32_t particleindex, uint8_t brightness, const CRGBW &color, bool wrap);
  void renderLargeParticle(uint32_t size, uint32_t particleindex, uint8_t brightness,
                           const CRGBW &color, bool wrap);

  // paricle physics applied by system if flags are set
  void applyGravity();  // applies gravity to all particles
  void handleCollisions();
  void collideParticles(uint32_t partIdx1, uint32_t partIdx2, int32_t dx, uint32_t collisiondistance);

  // utility functions
  void updatePSpointers(bool isadvanced);  // update the data pointers to current segment data space
  // void updateSize(PSadvancedParticle *advprops, PSsizeControl *advsize); // advanced size control
  [[gnu::hot]] void bounce(int8_t &incomingspeed, int8_t &parallelspeed, int32_t &position,
                           uint32_t maxposition);  // bounce on a wall
  // note: variables that are accessed often are 32bit for speed
  uint32_t *framebuffer_;  // frame buffer for rendering. note: using CRGBW as the buffer is slower, ESP compiler seems
                          // to optimize this better giving more consistent FPS
  PSsettings1D particlesettings_;  // settings used when updating particles
  uint32_t     numParticles_;      // total number of particles allocated by this system
  uint32_t     emitIndex_;         // index to count through particles to emit so searching for dead pixels is faster
  int32_t      collisionHardness_;
  uint32_t     particleHardRadius_;  // hard surface radius of a particle, used for collision detection
  uint32_t     wallHardness_;
  uint8_t      gforcecounter_;      // counter for global gravity
  int8_t       gforce_;             // gravity strength, default is 8 (negative is allowed, positive is downwards)
  uint8_t      forcecounter_;       // counter for globally applied forces
  uint16_t     collisionStartIdx_;  // particle array start index for collision detection
  // global particle properties for basic particles
  uint8_t particlesize_;  // global particle size, 0 = 1 pixel, 1 = 2 pixels, is overruled by advanced particle size
  uint8_t motionBlur_;    // enable motion blur, values > 100 gives smoother animations
  uint8_t smearBlur_;     // smeared blurring of full frame
};

bool     initParticleSystem1D(ParticleSystem1D *&PartSys, uint32_t requestedsources,
                              uint8_t fractionofparticles = 255, uint32_t additionalbytes = 0,
                              bool advanced = false);
uint32_t calculateNumberOfParticles1D(uint32_t fraction, bool isadvanced);
uint32_t calculateNumberOfSources1D(uint32_t requestedsources);
bool     allocateParticleSystemMemory1D(uint32_t numparticles, uint32_t numsources, bool isadvanced,
                                        uint32_t additionalbytes);

#endif  // WLED_DISABLE_PARTICLESYSTEM1D
