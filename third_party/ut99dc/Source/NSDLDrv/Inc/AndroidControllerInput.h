#pragma once

#include "SDL_events.h"
#include "SDL_gamecontroller.h"
#include <map>

inline bool UT99AndroidIsDuplicateJoystickEvent( const SDL_Event& Event )
{
    SDL_JoystickID Instance;
    switch( Event.type )
    {
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            Instance = Event.jbutton.which;
            break;
        case SDL_JOYAXISMOTION:
            Instance = Event.jaxis.which;
            break;
        case SDL_JOYHATMOTION:
            Instance = Event.jhat.which;
            break;
        default:
            return false;
    }
    return SDL_GameControllerFromInstanceID( Instance ) != nullptr;
}

inline Uint8 UT99AndroidDPadDirection( Uint8 Button )
{
    switch( Button )
    {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return SDL_HAT_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return SDL_HAT_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return SDL_HAT_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return SDL_HAT_RIGHT;
        default: return SDL_HAT_CENTERED;
    }
}

struct FAndroidDPadEdges
{
    Uint8 Pressed;
    Uint8 Released;
};

class FAndroidDPadState
{
public:
    void SetButton( SDL_JoystickID Instance, Uint8 Direction, bool Down )
    {
        const auto It = Buttons.find( Instance );
        const Uint8 Old = It != Buttons.end() ? It->second : SDL_HAT_CENTERED;
        const Uint8 Now = Down ? Old | Direction : Old & ~Direction;
        if( Now )
            Buttons[Instance] = Now;
        else
            Buttons.erase( Instance );
    }

    void SetHat( SDL_JoystickID Instance, Uint8 Hat )
    {
        if( Hat )
            Hats[Instance] = Hat;
        else
            Hats.erase( Instance );
    }

    void RemoveDevice( SDL_JoystickID Instance )
    {
        Buttons.erase( Instance );
        Hats.erase( Instance );
    }

    void ClearDevices()
    {
        Buttons.clear();
        Hats.clear();
    }

    FAndroidDPadEdges Update( Uint8 Direct, bool Menu )
    {
        const Uint8 Now = Combined( Direct );
        FAndroidDPadEdges Edges = { Uint8(Now & ~Previous), Uint8(GameplayDown & ~Now) };
        // Retain Previous across mode changes: a held menu key is not a new gameplay press.
        if( Menu != WasMenu )
        {
            Edges.Released |= GameplayDown;
            GameplayDown = SDL_HAT_CENTERED;
        }
        if( !Menu )
            GameplayDown = (GameplayDown | Edges.Pressed) & Now;
        Previous = Now;
        WasMenu = Menu;
        return Edges;
    }

    void Reset( Uint8 Direct, bool Menu )
    {
        Previous = Combined( Direct );
        GameplayDown = SDL_HAT_CENTERED;
        WasMenu = Menu;
    }

private:
    Uint8 Combined( Uint8 Direct ) const
    {
        for( const auto& Source : Buttons )
            Direct |= Source.second;
        for( const auto& Source : Hats )
            Direct |= Source.second;
        return Direct;
    }

    std::map<SDL_JoystickID, Uint8> Buttons;
    std::map<SDL_JoystickID, Uint8> Hats;
    Uint8 Previous = SDL_HAT_CENTERED;
    Uint8 GameplayDown = SDL_HAT_CENTERED;
    bool WasMenu = false;
};
