/*
 *      Copyright (C) 2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "SeekHandler.h"

#include <stdlib.h>

#include "Application.h"
#include "FileItem.h"
#include "guilib/GraphicContext.h"
#include "guilib/LocalizeStrings.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"

CSeekHandler::CSeekHandler()
: m_seekDelay(500),
  m_requireSeek(false),
  m_analogSeek(false),
  m_seekSize(0),
  m_seekStep(0)
{
}

CSeekHandler::~CSeekHandler()
{
  m_seekDelays.clear();
  m_forwardSeekSteps.clear();
  m_backwardSeekSteps.clear();
}

CSeekHandler& CSeekHandler::GetInstance()
{
  static CSeekHandler instance;
  return instance;
}

void CSeekHandler::Configure()
{
  Reset();

  m_seekDelays.clear();
  m_seekDelays.insert(std::make_pair(SEEK_TYPE_VIDEO, g_guiSettings.GetInt("videoplayer.seekdelay")));
  m_seekDelays.insert(std::make_pair(SEEK_TYPE_MUSIC, g_guiSettings.GetInt("musicplayer.seekdelay")));

  m_forwardSeekSteps.clear();
  m_backwardSeekSteps.clear();

  std::vector<int> forwardSeekSteps;
  forwardSeekSteps.push_back(30);
  forwardSeekSteps.push_back(60);
  m_forwardSeekSteps.insert(std::make_pair(SEEK_TYPE_MUSIC, forwardSeekSteps));
  forwardSeekSteps.push_back(120);
  forwardSeekSteps.push_back(180);
  forwardSeekSteps.push_back(300);
  m_forwardSeekSteps.insert(std::make_pair(SEEK_TYPE_VIDEO, forwardSeekSteps));

  std::vector<int> backwardSeekSteps;
  backwardSeekSteps.push_back(-15);
  backwardSeekSteps.push_back(-30);
  backwardSeekSteps.push_back(-60);
  m_backwardSeekSteps.insert(std::make_pair(SEEK_TYPE_MUSIC, backwardSeekSteps));
  backwardSeekSteps.push_back(-120);
  backwardSeekSteps.push_back(-180);
  backwardSeekSteps.push_back(-300);
  m_backwardSeekSteps.insert(std::make_pair(SEEK_TYPE_VIDEO, backwardSeekSteps));
}

void CSeekHandler::Reset()
{
  m_requireSeek = false;
  m_analogSeek = false;
  m_seekStep = 0;
  m_seekSize = 0;
  m_timer.Stop();
}

int CSeekHandler::GetSeekStepSize(SeekType type, int step)
{
  if (step == 0)
    return 0;

  std::vector<int> seekSteps(step > 0 ? m_forwardSeekSteps.at(type) : m_backwardSeekSteps.at(type));

  if (seekSteps.empty())
  {
    CLog::Log(LOGERROR, "SeekHandler - %s - No %s %s seek steps configured.", __FUNCTION__,
              (type == SeekType::SEEK_TYPE_VIDEO ? "video" : "music"), (step > 0 ? "forward" : "backward"));
    return 0;
  }

  int seconds = 0;

  // when exceeding the selected amount of steps repeat/sum up the last step size
  if (static_cast<size_t>(abs(step)) <= seekSteps.size())
    seconds = seekSteps.at(abs(step) - 1);
  else
    seconds = seekSteps.back() * (abs(step) - seekSteps.size() + 1);

  return seconds;
}

void CSeekHandler::Seek(bool forward, float amount, float duration /* = 0 */, bool analogSeek /* = false */, SeekType type /* = SEEK_TYPE_VIDEO */)
{
  CSingleLock lock(m_critSection);

  // not yet seeking
  if (!m_requireSeek)
  {
    // use only the first step forward/backward for a seek without a delay
    if (!analogSeek && m_seekDelays.at(type) == 0)
    {
      SeekSeconds(GetSeekStepSize(type, forward ? 1 : -1));
      return;
    }

    m_requireSeek = true;
    m_analogSeek = analogSeek;
    m_seekDelay = analogSeek ? analogSeekDelay : m_seekDelays.at(type);

    if (g_application.m_pPlayer->IsPlayingVideo() && !g_application.m_pPlayer->IsPaused())
      g_application.m_pPlayer->SetPlaySpeed(0, g_settings.m_bMute);
  }

  // calculate our seek amount
  if (analogSeek)
  {
    //100% over 1 second.
    float speed = 100.0f;
    if( duration )
      speed *= duration;
    else
      speed /= g_graphicsContext.GetFPS();

    double totalTime = g_application.GetTotalTime();
    if (totalTime < 0)
      totalTime = 0;

    int seekSize = (amount * amount * speed) * totalTime / 100;
    if (forward)
      m_seekSize += seekSize;
    else
      m_seekSize -= seekSize;
  }
  else
  {
    m_seekStep += forward ? 1 : -1;
    int seekSeconds = GetSeekStepSize(type, m_seekStep);
    if (seekSeconds != 0)
    {
      m_seekSize = seekSeconds;
    }
    else
    {
      if (g_application.m_pPlayer->IsPlayingVideo())
        g_application.m_pPlayer->SetPlaySpeed(1, g_settings.m_bMute);

      // nothing to do, abort seeking
      Reset();
      return;
    }
  }

  m_timer.StartZero();
}

void CSeekHandler::SeekSeconds(int seconds)
{
  // abort if we do not have a play time or already perform a seek
  if (seconds == 0)
    return;

  CSingleLock lock(m_critSection);
  m_seekSize = seconds;

  // perform relative seek
  g_application.m_pPlayer->SeekTimeRelative(static_cast<int64_t>(seconds * 1000));

  Reset();
}

int CSeekHandler::GetSeekSize() const
{
  return m_seekSize;
}

bool CSeekHandler::InProgress() const
{
  return m_requireSeek;
}

void CSeekHandler::Process()
{
  if (m_timer.GetElapsedMilliseconds() >= m_seekDelay && m_requireSeek)
  {
    CSingleLock lock(m_critSection);

    // perform relative seek
    g_application.m_pPlayer->SeekTimeRelative(static_cast<int64_t>(m_seekSize * 1000));

    if (g_application.m_pPlayer->IsPlayingVideo())
      g_application.m_pPlayer->SetPlaySpeed(1, g_settings.m_bMute);

    Reset();
  }
}

bool CSeekHandler::OnAction(const CAction &action)
{
  if (!g_application.m_pPlayer->IsPlaying() || !g_application.m_pPlayer->CanSeek())
    return false;

  SeekType type = g_application.CurrentFileItem().IsAudio() ? SEEK_TYPE_MUSIC : SEEK_TYPE_VIDEO;

  switch (action.GetID())
  {
    case ACTION_SMALL_STEP_BACK:
    case ACTION_STEP_BACK:
    {
      Seek(false, action.GetAmount(), action.GetRepeat(), false, type);
      return true;
    }
    case ACTION_STEP_FORWARD:
    {
      Seek(true, action.GetAmount(), action.GetRepeat(), false, type);
      return true;
    }
    case ACTION_BIG_STEP_BACK:
    case ACTION_CHAPTER_OR_BIG_STEP_BACK:
    {
      g_application.m_pPlayer->Seek(false, true, action.GetID() == ACTION_CHAPTER_OR_BIG_STEP_BACK);
      return true;
    }
    case ACTION_BIG_STEP_FORWARD:
    case ACTION_CHAPTER_OR_BIG_STEP_FORWARD:
    {
      g_application.m_pPlayer->Seek(true, true, action.GetID() == ACTION_CHAPTER_OR_BIG_STEP_FORWARD);
      return true;
    }
    case ACTION_NEXT_SCENE:
    {
      g_application.m_pPlayer->SeekScene(true);
      return true;
    }
    case ACTION_PREV_SCENE:
    {
      g_application.m_pPlayer->SeekScene(false);
      return true;
    }
    case ACTION_ANALOG_SEEK_FORWARD:
    case ACTION_ANALOG_SEEK_BACK:
    {
      if (action.GetAmount())
        Seek(action.GetID() == ACTION_ANALOG_SEEK_FORWARD, action.GetAmount(), action.GetRepeat(), true);
      return true;
    }
    default:
      break;
  }

  return false;
}
