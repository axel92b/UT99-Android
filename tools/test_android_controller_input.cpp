#include "AndroidControllerInput.h"
#include <cassert>
#include <cstdio>

struct _SDL_GameController {};
static SDL_GameController Controller;
static SDL_JoystickID LastLookup = -1;

extern "C" SDL_GameController* SDLCALL SDL_GameControllerFromInstanceID( SDL_JoystickID Instance )
{
    LastLookup = Instance;
    return Instance == 42 ? &Controller : nullptr;
}

static void ExpectEdges( FAndroidDPadState& State, Uint8 Direct, bool Menu,
                         Uint8 Pressed, Uint8 Released )
{
    const FAndroidDPadEdges Edges = State.Update( Direct, Menu );
    assert( Edges.Pressed == Pressed );
    assert( Edges.Released == Released );
}

static void TestRawEventOwnership()
{
    for( Uint32 Type : { SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP, SDL_JOYAXISMOTION, SDL_JOYHATMOTION } )
    {
        for( SDL_JoystickID Instance : { 42, 77 } )
        {
            SDL_Event Event = {};
            Event.type = Type;
            if( Type == SDL_JOYAXISMOTION )
            {
                Event.jaxis.which = Instance;
                Event.jaxis.axis = SDL_CONTROLLER_AXIS_LEFTX;
                Event.jaxis.value = -32767;
            }
            else if( Type == SDL_JOYHATMOTION )
            {
                Event.jhat.which = Instance;
                Event.jhat.value = SDL_HAT_LEFTUP;
            }
            else
            {
                Event.jbutton.which = Instance;
                Event.jbutton.button = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
            }
            assert( UT99AndroidIsDuplicateJoystickEvent( Event ) == (Instance == 42) );
            assert( LastLookup == Instance );
        }
    }

    for( Uint32 Type : { SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERBUTTONDOWN,
                        SDL_CONTROLLERBUTTONUP, SDL_CONTROLLERDEVICEADDED,
                        SDL_CONTROLLERDEVICEREMOVED, SDL_JOYDEVICEREMOVED,
                        SDL_KEYDOWN, SDL_MOUSEMOTION } )
    {
        SDL_Event Event = {};
        Event.type = Type;
        LastLookup = -1;
        assert( !UT99AndroidIsDuplicateJoystickEvent( Event ) );
        assert( LastLookup == -1 );
    }
}

static void TestDPadButtonMapping()
{
    for( int Button = 0; Button < SDL_CONTROLLER_BUTTON_MAX; ++Button )
    {
        Uint8 Expected = SDL_HAT_CENTERED;
        switch( Button )
        {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    Expected = SDL_HAT_UP; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  Expected = SDL_HAT_DOWN; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  Expected = SDL_HAT_LEFT; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: Expected = SDL_HAT_RIGHT; break;
        }
        assert( UT99AndroidDPadDirection( Uint8(Button) ) == Expected );
    }
}

static void TestOverlappingSources()
{
    for( Uint8 Direction : { SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT } )
    {
        for( bool SdlFirst : { false, true } )
        {
            for( bool SdlReleasedFirst : { false, true } )
            {
                FAndroidDPadState State;
                Uint8 Direct = SDL_HAT_CENTERED;
                if( SdlFirst )
                    State.SetButton( 42, Direction, true );
                else
                    Direct = Direction;
                ExpectEdges( State, Direct, false, Direction, 0 );

                State.SetButton( 42, Direction, true );
                Direct = Direction;
                ExpectEdges( State, Direct, false, 0, 0 );
                ExpectEdges( State, Direct, false, 0, 0 );

                if( SdlReleasedFirst )
                    State.SetButton( 42, Direction, false );
                else
                    Direct = SDL_HAT_CENTERED;
                ExpectEdges( State, Direct, false, 0, 0 );

                Direct = SDL_HAT_CENTERED;
                State.SetButton( 42, Direction, false );
                ExpectEdges( State, Direct, false, 0, Direction );
                ExpectEdges( State, Direct, false, 0, 0 );
            }
        }
    }

    FAndroidDPadState State;
    State.SetHat( 77, SDL_HAT_LEFTUP );
    ExpectEdges( State, 0, false, SDL_HAT_LEFTUP, 0 );
    State.SetButton( 77, SDL_HAT_LEFT, true );
    ExpectEdges( State, 0, false, 0, 0 );
    State.SetHat( 77, SDL_HAT_CENTERED );
    ExpectEdges( State, 0, false, 0, SDL_HAT_UP );
    State.SetButton( 77, SDL_HAT_LEFT, false );
    ExpectEdges( State, 0, false, 0, SDL_HAT_LEFT );
}

static void TestMenuTransitions()
{
    FAndroidDPadState State;
    ExpectEdges( State, SDL_HAT_LEFT, false, SDL_HAT_LEFT, 0 );
    ExpectEdges( State, SDL_HAT_LEFT, true, 0, SDL_HAT_LEFT );
    ExpectEdges( State, SDL_HAT_LEFT, true, 0, 0 );
    ExpectEdges( State, 0, true, 0, 0 );
    ExpectEdges( State, SDL_HAT_RIGHT, true, SDL_HAT_RIGHT, 0 );
    ExpectEdges( State, SDL_HAT_RIGHT, true, 0, 0 );
    ExpectEdges( State, SDL_HAT_RIGHT, false, 0, 0 );
    ExpectEdges( State, 0, false, 0, 0 );
    ExpectEdges( State, SDL_HAT_RIGHT, false, SDL_HAT_RIGHT, 0 );
    ExpectEdges( State, 0, false, 0, SDL_HAT_RIGHT );

    ExpectEdges( State, SDL_HAT_LEFT, false, SDL_HAT_LEFT, 0 );
    ExpectEdges( State, SDL_HAT_RIGHT, true, SDL_HAT_RIGHT, SDL_HAT_LEFT );
    ExpectEdges( State, SDL_HAT_LEFT, false, SDL_HAT_LEFT, 0 );
    ExpectEdges( State, 0, false, 0, SDL_HAT_LEFT );
}

static void TestResetAndDisconnect()
{
    FAndroidDPadState State;
    State.SetButton( 42, SDL_HAT_LEFT, true );
    ExpectEdges( State, 0, false, SDL_HAT_LEFT, 0 );
    State.Reset( 0, false );
    ExpectEdges( State, 0, false, 0, 0 );
    State.SetButton( 42, SDL_HAT_LEFT, false );
    ExpectEdges( State, 0, false, 0, 0 );

    State.SetButton( 42, SDL_HAT_LEFT, true );
    ExpectEdges( State, 0, false, SDL_HAT_LEFT, 0 );
    State.SetButton( 77, SDL_HAT_LEFT, true );
    ExpectEdges( State, 0, false, 0, 0 );
    State.RemoveDevice( 42 );
    ExpectEdges( State, 0, false, 0, 0 );
    State.RemoveDevice( 77 );
    ExpectEdges( State, 0, false, 0, SDL_HAT_LEFT );

    State.SetButton( 88, SDL_HAT_RIGHT, true );
    ExpectEdges( State, 0, false, SDL_HAT_RIGHT, 0 );
    State.SetHat( 88, SDL_HAT_UP );
    ExpectEdges( State, 0, false, SDL_HAT_UP, 0 );
    State.ClearDevices();
    ExpectEdges( State, 0, false, 0, SDL_HAT_RIGHTUP );

    State.SetButton( 88, SDL_HAT_RIGHT, true );
    ExpectEdges( State, 0, true, SDL_HAT_RIGHT, 0 );
    State.ClearDevices();
    ExpectEdges( State, 0, true, 0, 0 );
}

static void TestQuickTapsAndDiagonals()
{
    FAndroidDPadState State;
    for( int Tap = 0; Tap < 3; ++Tap )
    {
        State.SetButton( 42, SDL_HAT_RIGHT, true );
        ExpectEdges( State, 0, false, SDL_HAT_RIGHT, 0 );
        State.SetButton( 42, SDL_HAT_RIGHT, false );
        ExpectEdges( State, 0, false, 0, SDL_HAT_RIGHT );
    }

    State.SetHat( 42, SDL_HAT_LEFTUP );
    ExpectEdges( State, 0, false, SDL_HAT_LEFTUP, 0 );
    State.SetHat( 42, SDL_HAT_RIGHTDOWN );
    ExpectEdges( State, 0, false, SDL_HAT_RIGHTDOWN, SDL_HAT_LEFTUP );
    State.RemoveDevice( 42 );
    ExpectEdges( State, 0, false, 0, SDL_HAT_RIGHTDOWN );
}

int main()
{
    TestRawEventOwnership();
    TestDPadButtonMapping();
    TestOverlappingSources();
    TestMenuTransitions();
    TestResetAndDisconnect();
    TestQuickTapsAndDiagonals();
    std::puts( "Android controller input regression checks passed." );
}
