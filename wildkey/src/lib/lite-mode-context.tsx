"use client";

import {
  createContext,
  useContext,
  useEffect,
  useState,
  type ReactNode,
} from "react";

const STORAGE_KEY = "wildkey.liteMode";

type LiteModeContextValue = {
  liteMode: boolean;
  setLiteMode: (value: boolean) => void;
};

const LiteModeContext = createContext<LiteModeContextValue | null>(null);

export function LiteModeProvider({ children }: { children: ReactNode }) {
  const [liteMode, setLiteModeState] = useState(false);

  useEffect(() => {
    const stored = window.localStorage.getItem(STORAGE_KEY);
    if (stored === "true") {
      // Restoring a persisted preference on mount — see the same note in
      // mode-context.tsx for why this doesn't need to be avoided.
      // eslint-disable-next-line react-hooks/set-state-in-effect
      setLiteModeState(true);
    }
  }, []);

  const setLiteMode = (value: boolean) => {
    setLiteModeState(value);
    window.localStorage.setItem(STORAGE_KEY, String(value));
  };

  return (
    <LiteModeContext.Provider value={{ liteMode, setLiteMode }}>
      {children}
    </LiteModeContext.Provider>
  );
}

export function useLiteMode() {
  const ctx = useContext(LiteModeContext);
  if (!ctx) throw new Error("useLiteMode must be used within a LiteModeProvider");
  return ctx;
}
