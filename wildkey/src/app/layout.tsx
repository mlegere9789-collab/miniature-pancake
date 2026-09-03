import type { Metadata } from "next";
import { Geist, Geist_Mono, Source_Serif_4 } from "next/font/google";
import "./globals.css";
import { ModeProvider } from "@/lib/mode-context";
import { AuthProvider } from "@/lib/auth-context";
import { LiteModeProvider } from "@/lib/lite-mode-context";
import { LocaleProvider } from "@/lib/locale-context";
import { TopBar } from "@/components/top-bar";
import { BottomNav } from "@/components/bottom-nav";

const geistSans = Geist({
  variable: "--font-geist-sans",
  subsets: ["latin"],
});

const geistMono = Geist_Mono({
  variable: "--font-geist-mono",
  subsets: ["latin"],
});

const sourceSerif = Source_Serif_4({
  variable: "--font-source-serif",
  subsets: ["latin"],
});

export const metadata: Metadata = {
  title: "Wildkey",
  description:
    "One app. Works everywhere. Never loses your data. Never lies to you about what it doesn't know.",
};

export default function RootLayout({ children }: LayoutProps<"/">) {
  return (
    <html
      lang="en"
      className={`${geistSans.variable} ${geistMono.variable} ${sourceSerif.variable} h-full antialiased`}
    >
      <body className="min-h-full flex flex-col font-sans">
        <ModeProvider>
          <AuthProvider>
            <LiteModeProvider>
              <LocaleProvider>
                <TopBar />
                <main className="flex flex-1 flex-col">{children}</main>
                <BottomNav />
              </LocaleProvider>
            </LiteModeProvider>
          </AuthProvider>
        </ModeProvider>
      </body>
    </html>
  );
}
