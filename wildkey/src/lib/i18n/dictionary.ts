export const LOCALES = ["en", "es", "fr"] as const;
export type Locale = (typeof LOCALES)[number];

export const LOCALE_LABELS: Record<Locale, string> = {
  en: "English",
  es: "Español",
  fr: "Français",
};

/**
 * Part C.1 asks for 20+ languages at launch. Shipping that honestly needs
 * professional translation or a licensed MT pipeline — neither of which
 * this environment has credentials for, and faking 20 languages with
 * untranslated or mistranslated strings would be worse than not claiming
 * it. What's real here instead: the actual i18n plumbing (a locale
 * context, a dictionary-based t() function, a switcher) proven against
 * three hand-written languages covering the highest-traffic screens (nav,
 * home, camera) — see docs/remaining-systems-design.md. Adding language
 * #4 through #20 later is purely a translation-content problem: drop a
 * new key into this file, no engineering work needed.
 */
export const DICTIONARY = {
  en: {
    "nav.explore": "Explore",
    "nav.identify": "Identify",
    "nav.observations": "My Observations",
    "nav.journal": "Journal",
    "nav.guides": "Guides",
    "nav.projects": "Projects",
    "nav.curator": "Curator",
    "home.tagline1": "One app. Works everywhere.",
    "home.tagline2": "Never loses your data.",
    "home.quickIdBody":
      "Point your camera at anything wild and find out what it is — no account, no ads, fully offline. When we're not sure, we say so.",
    "home.naturalistBody":
      "Log full observations, get community identifications, and contribute research-grade data — with a sync you can always see and trust.",
    "home.identifySomething": "Identify something",
    "home.logObservation": "Log an observation",
    "home.exploreNearby": "Explore sightings near you",
    "camera.title": "Identify something",
    "camera.subtitle":
      "Works fully offline. No account needed. Your photo stays on this device unless you choose to save it.",
    "camera.choosePhotoPlaceholder": "Take a photo or choose one from your library to identify.",
    "camera.chooseDifferentPhoto": "Choose a different photo",
    "camera.takeOrChoosePhoto": "Take or choose a photo",
    "camera.identify": "Identify",
    "camera.identifying": "Identifying…",
  },
  es: {
    "nav.explore": "Explorar",
    "nav.identify": "Identificar",
    "nav.observations": "Mis observaciones",
    "nav.journal": "Diario",
    "nav.guides": "Guías",
    "nav.projects": "Proyectos",
    "nav.curator": "Curador",
    "home.tagline1": "Una app. Funciona en todas partes.",
    "home.tagline2": "Nunca pierde tus datos.",
    "home.quickIdBody":
      "Apunta tu cámara a cualquier ser vivo y descubre qué es — sin cuenta, sin anuncios, totalmente sin conexión. Cuando no estamos seguros, te lo decimos.",
    "home.naturalistBody":
      "Registra observaciones completas, obtén identificaciones de la comunidad y contribuye con datos de nivel de investigación — con una sincronización que siempre puedes ver y en la que puedes confiar.",
    "home.identifySomething": "Identificar algo",
    "home.logObservation": "Registrar una observación",
    "home.exploreNearby": "Explorar avistamientos cercanos",
    "camera.title": "Identificar algo",
    "camera.subtitle":
      "Funciona totalmente sin conexión. No se necesita cuenta. Tu foto permanece en este dispositivo a menos que decidas guardarla.",
    "camera.choosePhotoPlaceholder": "Toma una foto o elige una de tu galería para identificarla.",
    "camera.chooseDifferentPhoto": "Elegir otra foto",
    "camera.takeOrChoosePhoto": "Tomar o elegir una foto",
    "camera.identify": "Identificar",
    "camera.identifying": "Identificando…",
  },
  fr: {
    "nav.explore": "Explorer",
    "nav.identify": "Identifier",
    "nav.observations": "Mes observations",
    "nav.journal": "Journal",
    "nav.guides": "Guides",
    "nav.projects": "Projets",
    "nav.curator": "Modérateur",
    "home.tagline1": "Une app. Fonctionne partout.",
    "home.tagline2": "Ne perd jamais vos données.",
    "home.quickIdBody":
      "Pointez votre appareil photo vers n'importe quel être vivant et découvrez ce que c'est — sans compte, sans publicité, entièrement hors ligne. Quand nous ne sommes pas sûrs, nous le disons.",
    "home.naturalistBody":
      "Enregistrez des observations complètes, obtenez des identifications de la communauté et contribuez à des données de qualité recherche — avec une synchronisation toujours visible et fiable.",
    "home.identifySomething": "Identifier quelque chose",
    "home.logObservation": "Enregistrer une observation",
    "home.exploreNearby": "Explorer les observations à proximité",
    "camera.title": "Identifier quelque chose",
    "camera.subtitle":
      "Fonctionne entièrement hors ligne. Aucun compte requis. Votre photo reste sur cet appareil, sauf si vous choisissez de l'enregistrer.",
    "camera.choosePhotoPlaceholder": "Prenez une photo ou choisissez-en une dans votre bibliothèque pour l'identifier.",
    "camera.chooseDifferentPhoto": "Choisir une autre photo",
    "camera.takeOrChoosePhoto": "Prendre ou choisir une photo",
    "camera.identify": "Identifier",
    "camera.identifying": "Identification…",
  },
} as const satisfies Record<Locale, Record<string, string>>;

export type TranslationKey = keyof (typeof DICTIONARY)["en"];
