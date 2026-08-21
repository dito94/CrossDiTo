#include "Activity.h"

#include "ActivityManager.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

RequestUpdateResult Activity::requestUpdateAndWait() { return activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  if (!activity) {
    LOG_ERR("ACT", "OOM: unable to start child activity from %s", name.c_str());
    return;
  }
  if (activityManager.pushActivity(std::move(activity))) {
    this->resultHandler = std::move(resultHandler);
  }
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finishAfterBackPress() {
  mappedInput.suppressNextBackRelease();
  finish();
}

void Activity::finish() { activityManager.popActivity(); }
