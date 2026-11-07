#ifndef SOLENSHADED_SESSION_MANAGER_HPP
#define SOLENSHADED_SESSION_MANAGER_HPP

#include "solenshaded/solitaire_types.hpp"
#include <string>

namespace solenshaded {

class SolitaireBoard;

class SessionManager {
public:
    static SessionManager& instance();

    bool hasSavedSession() const;
    bool loadSession(SolitaireBoard& board);
    bool saveSession(const SolitaireBoard& board);
    void clearSession();

    [[nodiscard]] std::string getSessionFilePath() const;

private:
    SessionManager() = default;
};

} // namespace solenshaded

#endif // SOLENSHADED_SESSION_MANAGER_HPP

