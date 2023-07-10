/*!
 @file WaveTrackShifter.cpp
 @brief headerless file injects method definitions for time shifting of WaveTrack
 */

#include "../../../ui/TimeShiftHandle.h"
#include "ViewInfo.h"
#include "WaveClip.h"
#include "WaveTrack.h"
#include "WaveChannelView.h"

#include <cassert>

class WaveTrackShifter final : public TrackShifter {
public:
   /*!
    @pre `track.IsLeader()`
    */
   WaveTrackShifter(WaveTrack &track)
      : mpTrack{ track.SharedPointer<WaveTrack>() }
   {
      InitIntervals();
   }
   ~WaveTrackShifter() override {}
   Track &GetTrack() const override {
      assert(mpTrack->IsLeader()); // by construction
      return *mpTrack;
   }

   HitTestResult HitTest(
      double time, const ViewInfo &viewInfo, HitTestParams* params) override
   {
      const auto pClip = [&]() -> std::shared_ptr<WaveClip> {
         for (auto clip : mpTrack->GetClips())
            if ((
               params && WaveChannelView::HitTest(
                  *clip, viewInfo, params->rect, { params->xx, params->yy })
            ) || (
               // WithinPlayRegion misses first sample, which breaks moving
               // "selected" clip. Probable WithinPlayRegion should be fixed
               // instead?
                clip->GetPlayStartTime() <= time &&
                 time < clip->GetPlayEndTime()
            ))
               return clip;
         return {};
      }();
      
      if (!pClip)
         return HitTestResult::Miss;

      auto t0 = viewInfo.selectedRegion.t0();
      auto t1 = viewInfo.selectedRegion.t1();
      if (mpTrack->IsSelected() && time >= t0 && time < t1) {
         // Unfix maybe many intervals (at least one because of test above)
         SelectInterval({ t0, t1 });
         return HitTestResult::Selection;
      }

      // Select just one interval
      UnfixIntervals([&](const auto &interval){
         return static_cast<const WaveTrack::Interval&>(interval).GetClip(0)
           == pClip;
      });
      
      return HitTestResult::Intervals;
   }

   void SelectInterval(const ChannelGroupInterval &interval) override
   {
      UnfixIntervals([&](auto &myInterval){
         // Use a slightly different test from CommonSelectInterval, rounding times
         // to exact samples according to the clip's rate
         auto &data = static_cast<const WaveTrack::Interval&>(myInterval);
         auto clip = data.GetClip(0).get();
         const auto c0 = mpTrack->TimeToLongSamples(clip->GetPlayStartTime());
         const auto c1 = mpTrack->TimeToLongSamples(clip->GetPlayEndTime());
         return 
             mpTrack->TimeToLongSamples(interval.Start()) < c1 && 
             mpTrack->TimeToLongSamples(interval.End()) > c0;
      });
   }

   bool SyncLocks() override { return true; }

   bool MayMigrateTo(Track &other) override
   {
      return TrackShifter::CommonMayMigrateTo(other);
   }

   double HintOffsetLarger(double desiredOffset) override
   {
      // set it to a sample point, and minimum of 1 sample point
      bool positive = (desiredOffset > 0);
      if (!positive)
         desiredOffset *= -1;
      double nSamples = rint(mpTrack->GetRate() * desiredOffset);
      nSamples = std::max(nSamples, 1.0);
      desiredOffset = nSamples / mpTrack->GetRate();
      if (!positive)
         desiredOffset *= -1;
      return desiredOffset;
   }
   
   double QuantizeOffset( double desiredOffset ) override
   {
      const auto rate = mpTrack->GetRate();
      // set it to a sample point
      return rint(desiredOffset * rate) / rate;
   }

   double AdjustOffsetSmaller(double desiredOffset) override
   {
      std::vector<WaveClip *> movingClips;
      for (auto &interval : MovingIntervals()) {
         auto &data = static_cast<WaveTrack::Interval&>(*interval);
         movingClips.push_back(data.GetClip(0).get());
      }
      double newAmount = 0;
      (void) mpTrack->CanOffsetClips(movingClips, desiredOffset, &newAmount);
      return newAmount;
   }

   Intervals Detach() override
   {
      auto pRight = mpTrack->GetChannel<WaveTrack>(1);
      for (auto &interval: mMoving) {
         auto &data = static_cast<WaveTrack::Interval&>(*interval);
         auto pClip = data.GetClip(0).get();
         // interval will still hold the clip, so ignore the return:
         (void) mpTrack->RemoveAndReturnClip(pClip);
         mMigrated.erase(pClip);
         if (const auto pClip1 = data.GetClip(1).get()) {
            (void) pRight->RemoveAndReturnClip(pClip1);
            mMigrated.erase(pClip1);
         }
      }
      return std::move(mMoving);
   }

   bool AdjustFit(
      const Track &otherTrack, const Intervals &intervals,
      double &desiredOffset, double tolerance) override
   {
      bool ok = true;
      auto pOtherWaveTrack = static_cast<const WaveTrack*>(&otherTrack);
      for (auto &interval: intervals) {
         auto &data = static_cast<WaveTrack::Interval&>(*interval);
         auto pClip = data.GetClip(0).get();
         ok = pOtherWaveTrack->CanInsertClip(pClip, desiredOffset, tolerance);
         if (!ok)
            break;
      }
      return ok;
   }

   bool Attach(Intervals intervals, double offset) override
   {
      for (auto &interval : intervals) {
         auto &data = static_cast<WaveTrack::Interval&>(*interval);
         WaveClipHolder clips[2];
         for (size_t ii : { 0, 1 }) {
            auto pTrack = mpTrack->GetChannel<WaveTrack>(ii);
            auto &pClip = clips[ii] = data.GetClip(ii);
            if (pClip) {
               // TODO wide wave tracks -- guarantee matching clip width
               if (!pTrack->AddClip(pClip))
                  return false;
            }
            mMigrated.insert(pClip.get());
         }
         if (offset == .0)
            mMoving.emplace_back(std::move(interval));
         else {
            for (auto pClip : clips)
               if (pClip)
                  pClip->Offset(offset);
            mMoving.emplace_back(std::make_shared<WaveTrack::Interval>(
               clips[0], clips[1]));
         }
      }
      return true;
   }

   bool FinishMigration() override
   {
      auto rate = mpTrack->GetRate();
      for (auto pClip : mMigrated) {
         // Now that user has dropped the clip into a different track,
         // make sure the sample rate matches the destination track.
         pClip->Resample(rate);
         pClip->MarkChanged();
      }
      return true;
   }

   void DoHorizontalOffset(double offset) override
   {
      for (auto &interval : MovingIntervals()) {
         auto &data = static_cast<WaveTrack::Interval&>(*interval);
         data.GetClip(0)->Offset(offset);
         if (const auto pClip1 = data.GetClip(1))
            pClip1->Offset(offset);
      }
   }


   // Ensure that t0 is still within the clip which it was in before the move.
   // This corrects for any rounding errors.
   double AdjustT0(double t0) const override
   {
      if (MovingIntervals().empty())
         return t0;
      else {
         auto &data =
            static_cast<WaveTrack::Interval&>(*MovingIntervals()[0]);
         auto& clip = data.GetClip(0);
         t0 = std::clamp(t0, clip->GetPlayStartTime(), clip->GetPlayEndTime());
      }
      return t0;
   }
   
private:
   const std::shared_ptr<WaveTrack> mpTrack;

   // Clips that may require resampling
   std::unordered_set<WaveClip *> mMigrated;
};

using MakeWaveTrackShifter = MakeTrackShifter::Override<WaveTrack>;
DEFINE_ATTACHED_VIRTUAL_OVERRIDE(MakeWaveTrackShifter) {
   return [](WaveTrack &track, AudacityProject&) {
      assert(track.IsLeader()); // pre of the open method
      return std::make_unique<WaveTrackShifter>(track);
   };
}
