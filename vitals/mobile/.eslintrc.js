module.exports = {
  extends: ["expo"],
  ignorePatterns: ["/node_modules", "/.expo", "/dist"],
  rules: {
    "@typescript-eslint/no-unused-vars": ["warn", { argsIgnorePattern: "^_", varsIgnorePattern: "^_" }],
  },
};
