/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "Game.h"
#include "../resource.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <SDL_mixer.h>
#include "State.h"
#include "Screen.h"
#include "Sound.h"
#include "Music.h"
#include "Language.h"
#include "Logger.h"
#include "../Interface/Cursor.h"
#include "../Interface/FpsCounter.h"
#include "../Mod/Mod.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "Action.h"
#include "Exception.h"
#include "Options.h"
#include "CrossPlatform.h"
#include "FileMap.h"
#include "Unicode.h"
#include "../Ufopaedia/UfopaediaStartState.h"
#include "../Menu/NotesState.h"
#include "../Geoscape/GeoscapeState.h"
#include "../Menu/TestState.h"
#include <algorithm>
#include "../fallthrough.h"
#include "../Geoscape/GeoscapeState.h"

namespace OpenXcom
{

const double Game::VOLUME_GRADIENT = 10.0;

/**
 * Starts up all the SDL subsystems,
 * creates the display screen and sets up the cursor.
 * @param title Title of the game window.
 */
Game::Game(const std::string &title) : _screen(0), _cursor(0), _lang(0), _save(0), _mod(0), _quit(false), _init(false), _update(false),  _mouseActive(true), _timeUntilNextFrame(0),
	_ctrl(false), _alt(false), _shift(false), _rmb(false), _mmb(false), _scrollStep(1)
{
	Options::reload = false;
	Options::mute = false;

	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		Log(LOG_ERROR) << SDL_GetError();
		Log(LOG_WARNING) << "No video detected, quit.";
		throw Exception(SDL_GetError());
	}
	Log(LOG_INFO) << "SDL initialized successfully.";

	// Initialize SDL_mixer
	initAudio();

	// trap the mouse inside the window
	SDL_WM_GrabInput(Options::captureMouse);

	// Set the window icon
	CrossPlatform::setWindowIcon(IDI_ICON1, "openxcom.png");

	// Set the window caption
	SDL_WM_SetCaption(title.c_str(), 0);

	// Set up unicode
	SDL_EnableUNICODE(1);
	Unicode::getUtf8Locale();

	// Create display
	_screen = new Screen();

	// Create cursor
	_cursor = new Cursor(9, 13);

	// Create invisible hardware cursor to workaround bug with absolute positioning pointing devices
	SDL_ShowCursor(SDL_ENABLE);
	Uint8 cursor = 0;
	SDL_SetCursor(SDL_CreateCursor(&cursor, &cursor, 1,1,0,0));

	// Create fps counter
	_fpsCounter = new FpsCounter(15, 5, 0, 0);

	// Create blank language
	_lang = new Language();

	_timeOfLastFrame = 0;
}

/**
 * Deletes the display screen, cursor, states and shuts down all the SDL subsystems.
 */
Game::~Game()
{
	Sound::stop();
	Music::stop();

	for (auto* state : _states)
	{
		delete state;
	}

	SDL_FreeCursor(SDL_GetCursor());

	delete _cursor;
	delete _lang;
	delete _save;
	delete _mod;
	delete _screen;
	delete _fpsCounter;

	Mix_CloseAudio();

	SDL_Quit();
}

/**
 * The state machine takes care of passing all the events from SDL to the
 * active state, running any code within and blitting all the states and
 * cursor to the screen. This is run indefinitely until the game quits.
 */
void Game::run()
{
	enum ApplicationState { RUNNING = 0, SLOWED = 1, PAUSED = 2 } runningState = RUNNING;
	static const ApplicationState kbFocusRun[4] = { RUNNING, RUNNING, SLOWED, PAUSED };
	static const ApplicationState stateRun[4] = { SLOWED, PAUSED, PAUSED, PAUSED };
	// this will avoid processing SDL's resize event on startup, workaround for the heap allocation error it causes.
	bool startupEvent = Options::allowResize;
	Uint32 lastMouseMoveEvent = 0;
	Sint16 xrel = 0;
	Sint16 yrel = 0;

	while (!_quit)
	{
		// Clean up states
		while (!_deleted.empty())
		{
			delete _deleted.back();
			_deleted.pop_back();
		}

		// Initialize active state
		if (!_init)
		{
			_init = true;
			_states.back()->init();

			// Unpress buttons
			_states.back()->resetAll();

			// Refresh mouse position
			SDL_Event ev;
			int x, y;
			SDL_GetMouseState(&x, &y);
			ev.type = SDL_MOUSEMOTION;
			ev.motion.x = x;
			ev.motion.y = y;
			Action action = Action(&ev, _screen->getXScale(), _screen->getYScale(), _screen->getCursorTopBlackBand(), _screen->getCursorLeftBlackBand());
			_states.back()->handle(&action);
		}

		// Process events
		while (SDL_PollEvent(&_event))
		{
			if (CrossPlatform::isQuitShortcut(_event))
				_event.type = SDL_QUIT;
			switch (_event.type)
			{
				case SDL_QUIT:
					quit();
					break;
				case SDL_ACTIVEEVENT:
					// An event other than SDL_APPMOUSEFOCUS change happened.
					if (reinterpret_cast<SDL_ActiveEvent*>(&_event)->state & ~SDL_APPMOUSEFOCUS)
					{
						Uint8 currentState = SDL_GetAppState();
						// Game is minimized
						if (!(currentState & SDL_APPACTIVE))
						{
							runningState = stateRun[Options::pauseMode];
							if (Options::backgroundMute)
							{
								setVolume(0, 0, 0);
							}
						}
						// Game is not minimized but has no keyboard focus.
						else if (!(currentState & SDL_APPINPUTFOCUS))
						{
							runningState = kbFocusRun[Options::pauseMode];
							if (Options::backgroundMute)
							{
								setVolume(0, 0, 0);
							}
						}
						// Game has keyboard focus.
						else
						{
							runningState = RUNNING;
							if (Options::backgroundMute)
							{
								setVolume(Options::soundVolume, Options::musicVolume, Options::uiVolume);
							}
						}
					}
					break;
				case SDL_VIDEORESIZE:
					if (Options::allowResize)
					{
						if (!startupEvent)
						{
							Options::newDisplayWidth = Options::displayWidth = std::max(Screen::ORIGINAL_WIDTH, _event.resize.w);
							Options::newDisplayHeight = Options::displayHeight = std::max(Screen::ORIGINAL_HEIGHT, _event.resize.h);
							int dX = 0, dY = 0;
							Screen::updateScale(Options::battlescapeScale, Options::baseXBattlescape, Options::baseYBattlescape, false);
							Screen::updateScale(Options::geoscapeScale, Options::baseXGeoscape, Options::baseYGeoscape, false);
							for (auto* state : _states)
							{
								state->resize(dX, dY);
							}
							_screen->resetDisplay();
						}
						else
						{
							startupEvent = false;
						}
					}
					break;
				case SDL_MOUSEMOTION:
					if (Options::oxceThrottleMouseMoveEvent > 0)
					{
						Uint32 last = SDL_GetTicks();
						if (0 == lastMouseMoveEvent)
						{
							lastMouseMoveEvent = last;
						}
						if (last - lastMouseMoveEvent < (Uint32)Options::oxceThrottleMouseMoveEvent)
						{
							xrel += _event.motion.xrel;
							yrel += _event.motion.yrel;
							continue;
						}
						lastMouseMoveEvent = 0;
						_event.motion.xrel += std::exchange(xrel, 0);
						_event.motion.yrel += std::exchange(yrel, 0);
					}
					FALLTHROUGH;
				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
					// Skip mouse events if they're disabled
					if (!_mouseActive) continue;
					// re-gain focus on mouse-over or keypress.
					runningState = RUNNING;
					// Go on, feed the event to others
					FALLTHROUGH;
				default:
					Action action = Action(&_event, _screen->getXScale(), _screen->getYScale(), _screen->getCursorTopBlackBand(), _screen->getCursorLeftBlackBand());
					_screen->handle(&action);
					_cursor->handle(&action);
					_fpsCounter->handle(&action);
					if (action.getDetails()->type == SDL_KEYDOWN)
					{
						// "ctrl-g" grab input
						if (action.getDetails()->key.keysym.sym == SDLK_g && isCtrlPressed())
						{
							Options::captureMouse = (SDL_GrabMode)(!Options::captureMouse);
							SDL_WM_GrabInput(Options::captureMouse);
						}
						// "ctrl-n" notes UI
						else if (action.getDetails()->key.keysym.sym == SDLK_n && isCtrlPressed() && !isAltPressed())
						{
							if (_save && !containsNotesState())
							{
								if (_save->getSavedBattle())
								{
									if (!_save->getSavedBattle()->isBattlescapeStateBusy())
									{
										pushState(new NotesState(OPT_BATTLESCAPE));
									}
								}
								else
								{
									pushState(new NotesState(OPT_GEOSCAPE));
								}
							}
						}
						else if (Options::debug)
						{
							if (action.getDetails()->key.keysym.sym == SDLK_t && isCtrlPressed())
							{
								pushState(new TestState);
							}
							// "ctrl-u" debug UI
							else if (action.getDetails()->key.keysym.sym == SDLK_u && isCtrlPressed())
							{
								Options::debugUi = !Options::debugUi;
								_states.back()->redrawText();
							}
						}
					}
					_states.back()->handle(&action);
					break;
			}
			if (!_init)
			{
				// States stack was changed, break the loop so new state
				// can be initialized before processing new events
				break;
			}
		}

		// Process rendering
		if (runningState != PAUSED)
		{
			// Process logic
			_states.back()->think();
			_fpsCounter->think();
			if (Options::FPS > 0 && !(Options::useOpenGL && Options::vSyncForOpenGL))
			{
				// Update our FPS delay time based on the time of the last draw.
				int fps = SDL_GetAppState() & SDL_APPINPUTFOCUS ? Options::FPS : Options::FPSInactive;

				_timeUntilNextFrame = (1000.0f / fps) - (SDL_GetTicks() - _timeOfLastFrame);
			}
			else
			{
				_timeUntilNextFrame = 0;
			}

			if (_init && _timeUntilNextFrame <= 0)
			{
				// make a note of when this frame update occurred.
				_timeOfLastFrame = SDL_GetTicks();
				_fpsCounter->addFrame();
				_screen->clear();
				std::list<State*>::iterator i = _states.end();
				do
				{
					--i;
				}
				while (i != _states.begin() && !(*i)->isScreen());

				for (; i != _states.end(); ++i)
				{
					(*i)->blit();
				}
				_fpsCounter->blit(_screen->getSurface());
				_cursor->blit(_screen->getSurface());
				_screen->flip();
			}
		}

		// Save on CPU
		switch (runningState)
		{
			case RUNNING:
				SDL_Delay(1); //Save CPU from going 100%
				break;
			case SLOWED: case PAUSED:
				SDL_Delay(100); break; //More slowing down.
		}
	}

	Options::save();
}

/**
 * Stops the state machine and the game is shut down.
 */
void Game::quit()
{
	// Hard-learned lesson: there's a billion+ situations, where this causes a corrupted save and subsequent crashes. It's not worth it!
#if 0
	// Always save ironman
	if (_save != 0 && _save->isIronman() && !_save->getName().empty())
	{
		std::string filename = CrossPlatform::sanitizeFilename(_save->getName()) + ".sav";
		_save->save(filename, _mod);
	}
#endif
	_quit = true;
}

/**
 * Changes the audio volume of the music and
 * sound effect channels.
 * @param sound Sound volume, from 0 to MIX_MAX_VOLUME.
 * @param music Music volume, from 0 to MIX_MAX_VOLUME.
 * @param ui UI volume, from 0 to MIX_MAX_VOLUME.
 */
void Game::setVolume(int sound, int music, int ui)
{
	if (!Options::mute)
	{
		if (sound >= 0)
		{
			sound = volumeExponent(sound) * (double)SDL_MIX_MAXVOLUME;
			Mix_Volume(-1, sound);
			if (_save && _save->getSavedBattle())
			{
				Mix_Volume(3, sound * _save->getSavedBattle()->getAmbientVolume());
			}
			else
			{
				// channel 3: reserved for ambient sound effect.
				Mix_Volume(3, sound / 2);
			}
			// channel 4: reserved for unit responses
			Mix_Volume(4, sound);
		}
		if (music >= 0)
		{
			music = volumeExponent(music) * (double)SDL_MIX_MAXVOLUME;
			Mix_VolumeMusic(music);
		}
		if (ui >= 0)
		{
			ui = volumeExponent(ui) * (double)SDL_MIX_MAXVOLUME;
			Mix_Volume(1, ui);
			Mix_Volume(2, ui);
		}
	}
}

double Game::volumeExponent(int volume)
{
	return (exp(log(Game::VOLUME_GRADIENT + 1.0) * volume / (double)SDL_MIX_MAXVOLUME) -1.0 ) / Game::VOLUME_GRADIENT;
}

/**
 * Pops all the states currently in stack and pushes in the new state.
 * A shortcut for cleaning up all the old states when they're not necessary
 * like in one-way transitions.
 * @param state Pointer to the new state.
 */
void Game::setState(State *state)
{
	while (!_states.empty())
	{
		popState();
	}
	pushState(state);
	_init = false;
}

/**
 * Pushes a new state into the top of the stack and initializes it.
 * The new state will be used once the next game cycle starts.
 * @param state Pointer to the new state.
 */
void Game::pushState(State *state)
{
	_states.push_back(state);
	_init = false;
}

/**
 * Pops the last state from the top of the stack. Since states
 * can't actually be deleted mid-cycle, it's moved into a separate queue
 * which is cleared at the start of every cycle, so the transition
 * is seamless.
 */
void Game::popState()
{
	_deleted.push_back(_states.back());
	_states.pop_back();
	_init = false;
}

/**
 * Sets a new saved game for the game to use.
 * @param save Pointer to the saved game.
 */
void Game::setSavedGame(SavedGame *save)
{
	delete _save;
	_save = save;
}

/**
 * Loads the mods specified in the game options.
 */
void Game::loadMods()
{
	Mod::resetGlobalStatics();
	delete _mod;
	_mod = new Mod();
	_mod->loadAll();
}

/**
 * Sets whether the mouse is activated.
 * If it is, mouse events are processed, otherwise
 * they are ignored and the cursor is hidden.
 * @param active Is mouse activated?
 */
void Game::setMouseActive(bool active)
{
	_mouseActive = active;
	_cursor->setVisible(active);
}

/**
 * Returns whether current state is *state
 * @param state The state to test against the stack state
 * @return Is state the current state?
 */
bool Game::isState(State *state) const
{
	return !_states.empty() && _states.back() == state;
}

/**
 * Returns whether a UfopaediaStartState is in the background.
 * @return Is there a UfopaediaStartState in the background?
 */
bool Game::containsUfopaediaStartState() const
{
	for (auto* state : _states)
	{
		auto* pedia = dynamic_cast<UfopaediaStartState*>(state);
		if (pedia)
		{
			return true;
		}
	}
	return false;
}

/**
 * Returns whether a NotesState is in the background.
 * @return Is there a NotesState in the background?
 */
bool Game::containsNotesState() const
{
	for (auto* state : _states)
	{
		auto* notes = dynamic_cast<NotesState*>(state);
		if (notes)
		{
			return true;
		}
	}
	return false;
}

/**
 * Returns the GeoscapeState from the background (if available).
 * @return Pointer to GeoscapeState, or nullptr if not available.
 */
GeoscapeState* Game::getGeoscapeState() const
{
	for (auto* state : _states)
	{
		auto* geoscape = dynamic_cast<GeoscapeState*>(state);
		if (geoscape)
		{
			return geoscape;
		}
	}
	return nullptr;
}

/**
 * Checks if the game is currently quitting.
 * @return whether the game is shutting down or not.
 */
bool Game::isQuitting() const
{
	return _quit;
}

/**
 * Loads the most appropriate languages
 * given current system and game options.
 */
void Game::loadLanguages()
{
	const std::string defaultLang = "en-US";
	std::string currentLang = defaultLang;

	// No language set, detect based on system
	if (Options::language.empty())
	{
		std::string locale = CrossPlatform::getLocale();
		std::string lang = locale.substr(0, locale.find_first_of('-'));
		// Try to load full locale
		if (Language::isSupported(locale) && FileMap::fileExists("Language/" + locale + ".yml"))
		{
			currentLang = locale;
		}
		else
		{
			// Try to load language locale
			if (Language::isSupported(lang) && FileMap::fileExists("Language/" + lang + ".yml"))
			{
				currentLang = lang;
			}
			// Give up, use default
			else
			{
				currentLang = defaultLang;
			}
		}
	}
	else
	{
		// Use options language
		if (FileMap::fileExists("Language/" + Options::language + ".yml"))
		{
			currentLang = Options::language;
		}
		// Language not found, use default
		else
		{
			currentLang = defaultLang;
		}
	}
	Options::language = currentLang;

	delete _lang;
	_lang = new Language();

	const std::string dirLanguage = "Language/";
	const std::string dirLanguageAndroid = "Language/Android/";
	const std::string dirLanguageOXCE = "Language/OXCE/";
	const std::string dirLanguageTechnical = "Language/Technical/";

	const std::string defaultLangYml = defaultLang + ".yml";
	const std::string currentLangYml = currentLang + ".yml";

	// get vertical VFS map slices for the four filenames,
	// then submit frecs in lockstep to the _lang->loadFile().

	auto slice = FileMap::getSlice(dirLanguage + defaultLangYml);
	auto sliceAndroid = FileMap::getSlice(dirLanguageAndroid + defaultLangYml);
	auto sliceOXCE = FileMap::getSlice(dirLanguageOXCE + defaultLangYml);
	auto sliceTechnical = FileMap::getSlice(dirLanguageTechnical + defaultLangYml);

	auto slice2 = FileMap::getSlice(dirLanguage + currentLangYml);
	auto sliceAndroid2 = FileMap::getSlice(dirLanguageAndroid + currentLangYml);
	auto sliceOXCE2 = FileMap::getSlice(dirLanguageOXCE + currentLangYml);
	auto sliceTechnical2 = FileMap::getSlice(dirLanguageTechnical + currentLangYml);

	bool twoLangs = currentLang != defaultLang;
	for (size_t i = 0; i < slice.size(); ++i) {
		if (slice[i]) { _lang->loadFile(slice[i]); }
		if (twoLangs && slice2[i]) { _lang->loadFile(slice2[i]); }
		if (sliceAndroid[i]) { _lang->loadFile(sliceAndroid[i]); }
		if (twoLangs && sliceAndroid2[i]) { _lang->loadFile(sliceAndroid2[i]); }
		if (sliceOXCE[i]) { _lang->loadFile(sliceOXCE[i]); }
		if (twoLangs && sliceOXCE2[i]) { _lang->loadFile(sliceOXCE2[i]); }
		if (sliceTechnical[i]) { _lang->loadFile(sliceTechnical[i]); }
		if (twoLangs && sliceTechnical2[i]) { _lang->loadFile(sliceTechnical2[i]); }
	}

	_lang->loadRule(_mod->getExtraStrings(), defaultLang);
	if (twoLangs)
		_lang->loadRule(_mod->getExtraStrings(), currentLang);
}

/**
 * Initializes the audio subsystem.
 */
void Game::initAudio()
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
	{
		Log(LOG_ERROR) << SDL_GetError();
		Log(LOG_WARNING) << "No sound device detected, audio disabled.";
		Options::mute = true;
		return;
	}

	Uint16 format = MIX_DEFAULT_FORMAT;
	if (Options::audioBitDepth == 8)
		format = AUDIO_S8;

	if (Options::audioSampleRate % 11025 != 0)
	{
		Log(LOG_WARNING) << "Custom sample rate " << Options::audioSampleRate << "Hz, audio that doesn't match will be distorted!";
		Log(LOG_WARNING) << "SDL_mixer only supports multiples of 11025Hz.";
	}
	int minChunk = Options::audioSampleRate / 11025 * 512;
	Options::audioChunkSize = std::max(minChunk, Options::audioChunkSize);

	if (Mix_OpenAudio(Options::audioSampleRate, format, MIX_DEFAULT_CHANNELS, Options::audioChunkSize) != 0)
	{
		Log(LOG_ERROR) << Mix_GetError();
		Log(LOG_WARNING) << "Sound device failed, audio disabled.";
		Options::mute = true;
	}
	else
	{
		Mix_AllocateChannels(16);
		// Set up reserved channels:
		// 0 = not used?
		// 1-2 = UI
		// 3 = ambient
		// 4 = unit responses (OXCE only)
		Mix_ReserveChannels(5);
		Mix_GroupChannels(1, 2, 0);
		Log(LOG_INFO) << "SDL_mixer initialized successfully.";
		setVolume(Options::soundVolume, Options::musicVolume, Options::uiVolume);
	}
}

/**
 * Is CTRL pressed?
 */
bool Game::isCtrlPressed(bool considerTouchButtons) const
{
	if (considerTouchButtons && _ctrl)
	{
		return true;
	}
	return (SDL_GetModState() & KMOD_CTRL) != 0;
}

/**
 * Is ALT pressed?
 */
bool Game::isAltPressed(bool considerTouchButtons) const
{
	if (considerTouchButtons && _alt)
	{
		return true;
	}
	return (SDL_GetModState() & KMOD_ALT) != 0;
}

/**
 * Is SHIFT pressed?
 */
bool Game::isShiftPressed(bool considerTouchButtons) const
{
	if (considerTouchButtons && _shift)
	{
		return true;
	}
	return (SDL_GetModState() & KMOD_SHIFT) != 0;
}

/**
 * Is LMB pressed?
 */
bool Game::isLeftClick(Action* action, bool considerTouchButtons) const
{
	if (considerTouchButtons)
	{
		return (action->getDetails()->button.button == SDL_BUTTON_LEFT) && !_rmb && !_mmb;
	}
	return (action->getDetails()->button.button == SDL_BUTTON_LEFT);
}

/**
 * Is RMB pressed?
 */
bool Game::isRightClick(Action* action, bool considerTouchButtons) const
{
	if (considerTouchButtons)
	{
		return (action->getDetails()->button.button == SDL_BUTTON_RIGHT) || ((action->getDetails()->button.button == SDL_BUTTON_LEFT) && _rmb);
	}
	return (action->getDetails()->button.button == SDL_BUTTON_RIGHT);
}

/**
 * Is MMB pressed?
 */
bool Game::isMiddleClick(Action* action, bool considerTouchButtons) const
{
	if (considerTouchButtons)
	{
		return (action->getDetails()->button.button == SDL_BUTTON_MIDDLE) || ((action->getDetails()->button.button == SDL_BUTTON_LEFT) && _mmb);
	}
	return (action->getDetails()->button.button == SDL_BUTTON_MIDDLE);
}

/**
 * Resets the touch button flags.
 */
void Game::resetTouchButtonFlags()
{
	_ctrl = false;
	_alt = false;
	_shift = false;
	_rmb = false;
	_mmb = false;
	_scrollStep = 1;
}

}
