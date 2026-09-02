import "express-async-errors";

import cors from "cors";
import express from "express";
import { checkinsRouter } from "./routes/checkins";
import { gardensRouter } from "./routes/gardens";
import { plantsRouter } from "./routes/plants";
import { speciesRouter } from "./routes/species";
import { treatmentPlansRouter } from "./routes/treatmentPlans";
import { uploadsRouter } from "./routes/uploads";

const app = express();
app.use(cors());
app.use(express.json({ limit: "10mb" }));

const storageDir = process.env.PHOTO_STORAGE_DIR || "./uploads";
app.use("/uploads", express.static(storageDir));

app.get("/health", (_req, res) => res.json({ ok: true }));
app.use("/plants", plantsRouter);
app.use("/checkins", checkinsRouter);
app.use("/gardens", gardensRouter);
app.use("/uploads", uploadsRouter);
app.use("/species", speciesRouter);
app.use("/treatment-plans", treatmentPlansRouter);

// Unmatched route → JSON 404 instead of Express's default HTML page.
app.use((_req, res) => {
  res.status(404).json({ error: "not found" });
});

// Catch-all error handler. Without `express-async-errors` above, a
// rejected promise in an async route handler would never reach here on
// Express 4 — the request would just hang forever with no response sent
// at all, since nothing calls next(err) automatically.
// eslint-disable-next-line @typescript-eslint/no-unused-vars
app.use((err: unknown, _req: express.Request, res: express.Response, _next: express.NextFunction) => {
  console.error(err);
  const isPrismaNotFound =
    typeof err === "object" && err !== null && "code" in err && (err as { code: unknown }).code === "P2025";
  if (isPrismaNotFound) {
    return res.status(404).json({ error: "not found" });
  }
  res.status(500).json({ error: "internal server error" });
});

const port = process.env.PORT ? Number(process.env.PORT) : 4000;
app.listen(port, () => {
  console.log(`Vitals API listening on :${port}`);
});
