// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/sparkle_backend.h"

#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>

#include <utility>

#include "arcium/browser/update/sparkle_event.h"
#include "arcium/browser/update/update_status.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/scoped_blocking_call.h"

// The framework is loaded at run time rather than linked, so that nothing of
// it is read before the first window paints, so that a build without it still
// starts, and so that this file names no Sparkle symbol the linker would have
// to find. Classes are therefore reached by name, and error domains compared
// as strings.

namespace {

NSString* const kSparkleErrorDomain = @"SUSparkleErrorDomain";

// A finished cycle that found nothing, or that the reader cancelled, is not a
// failure worth reporting.
bool IsQuietEnding(NSError* error) {
  return !error || ([error.domain isEqualToString:kSparkleErrorDomain] &&
                    (error.code == SUNoUpdateError ||
                     error.code == SUInstallationCanceledError));
}

}  // namespace

@interface KyuzenSparkleDelegate : NSObject <SPUUpdaterDelegate>
- (void)setReporter:(arcium::UpdaterBackend::StateCallback)reporter;
@end

@implementation KyuzenSparkleDelegate {
  arcium::UpdaterBackend::StateCallback _reporter;
  arcium::UpdateStatus::State _state;
}

- (void)setReporter:(arcium::UpdaterBackend::StateCallback)reporter {
  _reporter = std::move(reporter);
}

- (void)report:(arcium::SparkleEvent)event {
  _state = arcium::StateAfter(_state, event);
  if (_reporter) {
    _reporter.Run(_state);
  }
}

// The setting has already answered the question Sparkle would otherwise put
// to the reader on the second launch.
- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SPUUpdater*)updater {
  return NO;
}

- (BOOL)updater:(SPUUpdater*)updater
    mayPerformUpdateCheck:(SPUUpdateCheck)updateCheck
                    error:(NSError* __autoreleasing*)error {
  [self report:arcium::SparkleEvent::kCheckStarted];
  return YES;
}

- (void)updater:(SPUUpdater*)updater didFindValidUpdate:(SUAppcastItem*)item {
  [self report:arcium::SparkleEvent::kUpdateFound];
}

- (void)updater:(SPUUpdater*)updater
    willDownloadUpdate:(SUAppcastItem*)item
           withRequest:(NSMutableURLRequest*)request {
  [self report:arcium::SparkleEvent::kDownloadStarted];
}

- (void)updater:(SPUUpdater*)updater
    failedToDownloadUpdate:(SUAppcastItem*)item
                     error:(NSError*)error {
  [self report:arcium::SparkleEvent::kDownloadFailed];
}

- (void)updater:(SPUUpdater*)updater
    userDidMakeChoice:(SPUUserUpdateChoice)choice
            forUpdate:(SUAppcastItem*)updateItem
                state:(SPUUserUpdateState*)state {
  if (choice == SPUUserUpdateChoiceInstall) {
    return;
  }
  // Put off after it was already installing, it still installs on quit.
  if (state.stage == SPUUserUpdateStageInstalling) {
    [self report:arcium::SparkleEvent::kInstallScheduled];
    return;
  }
  [self report:arcium::SparkleEvent::kUpdateDeclined];
}

// Returning NO leaves installation to Sparkle, which replaces the application
// as it quits; the About page's relaunch is that quit.
- (BOOL)updater:(SPUUpdater*)updater
       willInstallUpdateOnQuit:(SUAppcastItem*)item
    immediateInstallationBlock:(void (^)(void))immediateInstallHandler {
  [self report:arcium::SparkleEvent::kInstallScheduled];
  return NO;
}

- (void)updater:(SPUUpdater*)updater
    didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck
                                 error:(NSError*)error {
  if (IsQuietEnding(error)) {
    [self report:arcium::SparkleEvent::kCycleFinished];
    return;
  }
  // A feed we broke, or no network: the browser carries on and the next
  // check tries again.
  LOG(WARNING) << "Update check did not finish: "
               << base::SysNSStringToUTF8(error.localizedDescription);
  [self report:arcium::SparkleEvent::kCycleFailed];
}

@end

namespace arcium {
namespace {

class SparkleBackend : public UpdaterBackend {
 public:
  explicit SparkleBackend(Class controller_class)
      : delegate_([[KyuzenSparkleDelegate alloc] init]) {
    // Not started yet: the setting reaches Sparkle first, so that its first
    // act follows the reader's choice rather than its own defaults.
    controller_ = [[controller_class alloc] initWithStartingUpdater:NO
                                                    updaterDelegate:delegate_
                                                 userDriverDelegate:nil];
  }
  SparkleBackend(const SparkleBackend&) = delete;
  SparkleBackend& operator=(const SparkleBackend&) = delete;
  ~SparkleBackend() override = default;

  // UpdaterBackend:
  void SetStateCallback(StateCallback callback) override {
    [delegate_ setReporter:std::move(callback)];
  }
  void SetChecksAutomatically(bool checks) override {
    controller_.updater.automaticallyChecksForUpdates = checks;
  }
  void SetDownloadsAutomatically(bool downloads) override {
    controller_.updater.automaticallyDownloadsUpdates = downloads;
  }
  void CheckInBackground() override {
    // Starting runs Sparkle's own schedule, which looks once a day and looks
    // now if a day has passed, so the first look of a run is the start.
    if (!started_) {
      Start();
      return;
    }
    [controller_.updater checkForUpdatesInBackground];
  }
  void CheckByHand() override {
    Start();
    [controller_ checkForUpdates:nil];
  }

 private:
  void Start() {
    if (started_) {
      return;
    }
    started_ = true;
    [controller_ startUpdater];
  }

  // Sparkle holds its delegate weakly.
  KyuzenSparkleDelegate* __strong delegate_;
  SPUStandardUpdaterController* __strong controller_;
  bool started_ = false;
};

}  // namespace

bool LoadSparkleFramework() {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  NSString* path = [NSBundle.mainBundle.privateFrameworksPath
      stringByAppendingPathComponent:@"Sparkle.framework"];
  NSBundle* bundle = path ? [NSBundle bundleWithPath:path] : nil;
  if (!bundle) {
    return false;
  }
  NSError* error = nil;
  if (![bundle loadAndReturnError:&error]) {
    LOG(ERROR) << "Sparkle is in the bundle but did not load: "
               << base::SysNSStringToUTF8(error.localizedDescription);
    return false;
  }
  return true;
}

std::unique_ptr<UpdaterBackend> MakeSparkleBackend() {
  Class controller_class = NSClassFromString(@"SPUStandardUpdaterController");
  if (!controller_class) {
    return nullptr;
  }
  return std::make_unique<SparkleBackend>(controller_class);
}

}  // namespace arcium
