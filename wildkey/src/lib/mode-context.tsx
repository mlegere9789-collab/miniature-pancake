"use client";

import {
  createContext,
  useContext,
  useEffect,
  useState,
  type ReactNode,
} from "react";

export type AppMode = "quick-id" | "naturalist";

const MODE_STORAGE_KEY = "wildkey.mode";

type ModeContextValue = {
  mode: AppMode;
  setMode: (mode: AppMode) => void;
  toggleMode: () => void;
};

const ModeContext = createContext<ModeContextValue | null>(null);

export function ModeProvider({ children }: { children: ReactNode }) {
  const [mode, setModeState] = useState<AppMode>("quick-id");

  useEffect(() => {
    const stored = window.localStorage.getItem(MODE_STORAGE_KEY);
    if (stored === "quick-id" || stored === "naturalist") {
      // Restoring a persisted preference on mount, not deriving state from
      // props/state — the pattern react-hooks/set-state-in-effect warns
      // about does not apply here.
      // eslint-disable-next-line react-hooks/set-state-in-effect
      setModeState(stored);
    }
  }, []);

  const setMode = (next: AppMode) => {
    setModeState(next);
    window.localStorage.setItem(MODE_STORAGE_KEY, next);
  };

  const toggleMode = () =>
    setMode(mode === "quick-id" ? "naturalist" : "quick-id");

  return (
    <ModeContext.Provider value={{ mode, setMode, toggleMode }}>
      {children}
    </ModeContext.Provider>
  );
}

export function useMode() {
  const ctx = useContext(ModeContext);
  if (!ctx) {
    throw new Error("useMode must be used within a ModeProvider");
  }
  return ctx;
}
