#include "note_log.h"

#include "theory.h"

void NoteLog::noteOn(int midi) {
  held_.insert(midi);
  group_.insert(midi);
}

void NoteLog::noteOff(int midi) {
  if (held_.erase(midi) == 0) return;
  if (held_.empty() && !group_.empty()) {
    entries_.push_back(theory::groupLabel({group_.begin(), group_.end()}));
    group_.clear();
  }
}

std::string NoteLog::pendingLabel() const {
  return theory::groupLabel({group_.begin(), group_.end()});
}

void NoteLog::undo() {
  if (!entries_.empty()) entries_.pop_back();
}

std::string NoteLog::text() const {
  std::string out;
  for (const auto& e : entries_) {
    if (!out.empty()) out += ' ';
    out += e;
  }
  return out;
}
