#pragma once

#include <set>
#include <string>
#include <vector>

// Turns note on/off events into a written log. Notes that overlap in time
// form one group; the group becomes a single entry ("C4", "Am7", "C4+D4")
// when the last of its notes is released.
class NoteLog {
 public:
  void noteOn(int midi);
  void noteOff(int midi);

  const std::vector<std::string>& entries() const { return entries_; }
  // Label of the group currently being held, or empty.
  std::string pendingLabel() const;
  // Notes currently held, ascending.
  std::vector<int> held() const { return {held_.begin(), held_.end()}; }

  void undo();
  void clear() { entries_.clear(); }
  // Entries joined by spaces, for the clipboard.
  std::string text() const;

 private:
  std::vector<std::string> entries_;
  std::set<int> held_;
  std::set<int> group_;
};
