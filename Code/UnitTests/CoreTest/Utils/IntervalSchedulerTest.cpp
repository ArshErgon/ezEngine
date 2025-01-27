#include <CoreTest/CoreTestPCH.h>

#include <Core/Utils/IntervalScheduler.h>

EZ_CREATE_SIMPLE_TEST_GROUP(Utils);

namespace
{
  struct TestWork
  {
    float m_IntervalMs = 0.0f;
    ezUInt32 m_Counter = 0;

    void Run()
    {
      ++m_Counter;
    }
  };
} // namespace

EZ_CREATE_SIMPLE_TEST(Utils, IntervalScheduler)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Constant workload")
  {
    float intervals[] = {10, 20, 60, 60, 60};

    ezHybridArray<TestWork, 32> works;
    ezIntervalScheduler<TestWork*> scheduler;

    for (ezUInt32 i = 0; i < EZ_ARRAY_SIZE(intervals); ++i)
    {
      auto& work = works.ExpandAndGetRef();
      work.m_IntervalMs = intervals[i];

      scheduler.AddOrUpdateWork(&work, ezTime::Milliseconds(work.m_IntervalMs));
    }

    ezUInt32 wrongDelta = 0;
    for (ezUInt32 i = 0; i < 60; ++i)
    {
      float fNumWorks = 0;
      scheduler.Update(ezTime::Milliseconds(10), [&](TestWork* pWork, ezTime deltaTime) {
        if (i > 10)
        {
          const double deltaMs = deltaTime.GetMilliseconds();
          const double variance = pWork->m_IntervalMs * 0.3;
          const double midValue = pWork->m_IntervalMs + 1.0 - variance;
          if (ezMath::IsEqual<double>(deltaMs, midValue, variance) == false)
          {
            ++wrongDelta;
          }
        }

        if (pWork->m_IntervalMs <= 10.0f)
        {
          EZ_TEST_INT(static_cast<ezUInt32>(pWork->m_Counter), i);
        }

        pWork->Run();
        ++fNumWorks;
      });

      EZ_TEST_FLOAT(fNumWorks, 2.5f, 0.5f);

      for (auto& work : works)
      {
        EZ_TEST_BOOL(scheduler.GetInterval(&work) == ezTime::Milliseconds(work.m_IntervalMs));
      }
    }

    // 2 wrong deltas for ~120 scheduled works is ok
    EZ_TEST_BOOL(wrongDelta <= 2);

    for (auto& work : works)
    {
      const float expectedCounter = 600.0f / ezMath::Max(work.m_IntervalMs, 10.0f);

      // check for roughly expected or a little bit more
      EZ_TEST_FLOAT(static_cast<float>(work.m_Counter), expectedCounter + 3.0f, 4.0f);
    }
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Constant workload (bigger delta)")
  {
    float intervals[] = {10, 20, 60, 60, 60};

    ezHybridArray<TestWork, 32> works;
    ezIntervalScheduler<TestWork*> scheduler;

    for (ezUInt32 i = 0; i < EZ_ARRAY_SIZE(intervals); ++i)
    {
      auto& work = works.ExpandAndGetRef();
      work.m_IntervalMs = intervals[i];

      scheduler.AddOrUpdateWork(&work, ezTime::Milliseconds(work.m_IntervalMs));
    }

    ezUInt32 wrongDelta = 0;
    for (ezUInt32 i = 0; i < 60; ++i)
    {
      float fNumWorks = 0;
      scheduler.Update(ezTime::Milliseconds(20), [&](TestWork* pWork, ezTime deltaTime) {
        if (i > 10)
        {
          const double deltaMs = deltaTime.GetMilliseconds();
          const double variance = ezMath::Max(pWork->m_IntervalMs, 20.0f) * 0.3;
          const double midValue = ezMath::Max(pWork->m_IntervalMs, 20.0f) + 1.0 - variance;
          if (ezMath::IsEqual<double>(deltaMs, midValue, variance) == false)
          {
            ++wrongDelta;
          }
        }

        if (pWork->m_IntervalMs <= 20.0f)
        {
          EZ_TEST_INT(static_cast<ezUInt32>(pWork->m_Counter), i);
        }

        pWork->Run();
        ++fNumWorks;
      });

      EZ_TEST_FLOAT(fNumWorks, 3.5f, 0.5f);

      for (auto& work : works)
      {
        EZ_TEST_BOOL(scheduler.GetInterval(&work) == ezTime::Milliseconds(work.m_IntervalMs));
      }
    }

    // 3 wrong deltas for ~150 scheduled works is ok
    EZ_TEST_BOOL(wrongDelta <= 3);

    for (auto& work : works)
    {
      const float expectedCounter = 1200.0f / ezMath::Max(work.m_IntervalMs, 20.0f);

      // check for roughly expected or a little bit more
      EZ_TEST_FLOAT(static_cast<float>(work.m_Counter), expectedCounter + 2.0f, 3.0f);
    }
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Dynamic workload")
  {
    ezHybridArray<TestWork, 32> works;

    ezIntervalScheduler<TestWork*> scheduler;

    for (ezUInt32 i = 0; i < 16; ++i)
    {
      auto& work = works.ExpandAndGetRef();
      scheduler.AddOrUpdateWork(&work, ezTime::Milliseconds(i));
    }

    for (ezUInt32 i = 0; i < 60; ++i)
    {
      float fNumWorks = 0;
      scheduler.Update(ezTime::Milliseconds(10), [&](TestWork* pWork, ezTime deltaTime) {
        pWork->Run();
        ++fNumWorks;
      });

      EZ_TEST_FLOAT(fNumWorks, 15.5f, 0.5f);
    }

    for (ezUInt32 i = 0; i < 16; ++i)
    {
      auto& work = works.ExpandAndGetRef();
      scheduler.AddOrUpdateWork(&work, ezTime::Milliseconds(20 + i));
    }

    float fPrevNumWorks = 15.5f;
    for (ezUInt32 i = 0; i < 60; ++i)
    {
      float fNumWorks = 0.0f;
      scheduler.Update(ezTime::Milliseconds(10), [&](TestWork* pWork, ezTime deltaTime) {
        pWork->Run();
        ++fNumWorks;
      });

      // fNumWork will slowly ramp up until it reaches the new workload of 22 or 23 per update
      EZ_TEST_BOOL(fNumWorks + 1.0f >= fPrevNumWorks);
      EZ_TEST_BOOL(fNumWorks <= 23.0f);

      fPrevNumWorks = fNumWorks;
    }

    for (ezUInt32 i = 0; i < 16; ++i)
    {
      auto& work = works[i];
      scheduler.RemoveWork(&work);
    }

    scheduler.Update(ezTime::Milliseconds(10), ezIntervalScheduler<TestWork*>::RunWorkCallback());

    for (ezUInt32 i = 0; i < 16; ++i)
    {
      auto& work = works[i + 16];
      EZ_TEST_BOOL(scheduler.GetInterval(&work) == ezTime::Milliseconds(20 + i));

      scheduler.AddOrUpdateWork(&work, ezTime::Milliseconds(100 + i));
    }

    scheduler.Update(ezTime::Milliseconds(10), ezIntervalScheduler<TestWork*>::RunWorkCallback());

    for (ezUInt32 i = 0; i < 16; ++i)
    {
      auto& work = works[i + 16];
      EZ_TEST_BOOL(scheduler.GetInterval(&work) == ezTime::Milliseconds(100 + i));
    }
  }
}
