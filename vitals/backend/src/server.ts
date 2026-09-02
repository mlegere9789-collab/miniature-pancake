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

const port = process.env.PORT ? Number(process.env.PORT) : 4000;
app.listen(port, () => {
  console.log(`Vitals API listening on :${port}`);
});
