#pragma once

namespace NWorld
{

// Periodic effects (SpellPeriodicallyApplicator, ChannellingApplicator) were tuned on the 10 Hz world step: they
// strike on the first step where the accumulated time is strictly greater than the period (timer > period), so the
// real interval is a whole number of 0.1 s steps and often one step longer than the period itself
// (0 = every step = 0.1 s, 0.1 = every second step = 0.2 s, 0.3 = 0.4 s because of float sums).
// With a shorter world step these effects keep the interval they had at 10 Hz: the period is translated once
// when the effect starts. Periods shorter than 0.1 s did not exist at 10 Hz (they struck every step) and are
// kept as set, down to PERIOD_MIN (0.05 s) - this is what the shorter step is for in the editor.
// With the 0.1 s step the original code is used unchanged.
const float PERIOD_10HZ_STEP = 0.1f; // the same float as the world step 100 / 1000.0f
const float PERIOD_MIN       = 0.05f;
const float PERIOD_EPS       = 0.0001f;

inline bool IsStepShorterThan10Hz( float stepInSeconds )
{
  return stepInSeconds < PERIOD_10HZ_STEP - 0.0005f;
}

// a period from the editor shorter than one 10 Hz step: new content for the shorter step
inline bool IsSubStepPeriod( float period )
{
  return period > 0.0f && period < PERIOD_10HZ_STEP - PERIOD_EPS;
}

// Replays the 10 Hz code "curPeriod = first; every 0.1 s: timer += 0.1; if ( timer > curPeriod ) { strike;
// timer = fmod( timer, curPeriod ); curPeriod = period; }" and returns the time of the first strike and the
// average interval after it. period <= 0 means a strike on every 10 Hz step.
inline void PeriodAt10Hz( float first, float period, float & firstOut, float & periodOut )
{
  firstOut = first;
  periodOut = period > 0.0f ? period : PERIOD_10HZ_STEP;
  if ( first > 100.0f || period > 100.0f ) // rounding to a step does not matter for such long periods
    return;

  // sub-step periods (new content) are kept as set, not shorter than PERIOD_MIN
  if ( IsSubStepPeriod( period ) )
    periodOut = Max( period, PERIOD_MIN );
  const bool firstIsSet = IsSubStepPeriod( first );
  if ( firstIsSet )
    firstOut = Max( first, PERIOD_MIN );
  if ( ( firstIsSet || first <= 0.0f ) && ( period <= 0.0f || IsSubStepPeriod( period ) ) )
    return; // nothing to replay

  float timer = 0.0f;
  float curPeriod = first > 0.0f && !firstIsSet ? first : period;
  int steps = 0, firstSteps = 0, strikes = 0;
  while ( steps < 2000 )
  {
    timer += PERIOD_10HZ_STEP;
    ++steps;
    if ( timer > curPeriod )
    {
      timer = fmodf( timer, curPeriod );
      if ( strikes == 0 )
      {
        firstSteps = steps;
        if ( period <= 0.0f || IsSubStepPeriod( period ) )
          break; // the rest is not replayed
        curPeriod = period;
      }
      ++strikes;
      if ( strikes > 20 || ( strikes > 1 && steps - firstSteps >= 200 ) )
        break;
    }
  }

  if ( !firstIsSet && strikes > 0 )
    firstOut = firstSteps * PERIOD_10HZ_STEP;
  if ( strikes > 1 )
    periodOut = ( steps - firstSteps ) * PERIOD_10HZ_STEP / ( strikes - 1 );
}

// One-shot delays counted down with a strict check "timer -= dt; if ( timer < 0 )" (ProgramApplicator delay,
// InvisibilityApplicator fadeIn) fire one step after an exact value: at 10 Hz a delay of 0.4 fired after 0.5 s,
// 0 after 0.1 s, and visuals were tuned to that. Replays the 10 Hz countdown and returns the time it fired at;
// delays shorter than 0.1 s did not exist at 10 Hz (they fired on the next step) and are kept as set, down to
// PERIOD_MIN. Negative delays (disabled) are returned as is.
inline float DelayAt10Hz( float delay )
{
  if ( delay < 0.0f || delay > 100.0f )
    return delay;
  if ( IsSubStepPeriod( delay ) )
    return Max( delay, PERIOD_MIN );

  float timer = delay;
  int steps = 0;
  do
  {
    timer -= PERIOD_10HZ_STEP;
    ++steps;
  } while ( !( timer < 0.0f ) && steps < 2000 );
  return steps * PERIOD_10HZ_STEP;
}

// Start value for such a countdown on a short world step: the strict check "< 0" fires on the first step
// where the elapsed time reaches the 10 Hz moment.
inline float DelayCountdownAt10Hz( float delay )
{
  return delay < 0.0f ? delay : DelayAt10Hz( delay ) - PERIOD_EPS;
}

// Strike check for a translated period on a short world step: the interval is set exactly,
// so it is compared without the float luck of "timer > period".
inline bool IsPeriodPassed( float timer, float period )
{
  return timer >= period - PERIOD_EPS;
}

} // namespace NWorld
