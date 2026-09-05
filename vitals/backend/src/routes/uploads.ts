import { Router } from "express";
import fs from "fs";
import multer from "multer";
import path from "path";

const storageDir = process.env.PHOTO_STORAGE_DIR || "./uploads";
fs.mkdirSync(storageDir, { recursive: true });

const upload = multer({
  storage: multer.diskStorage({
    destination: storageDir,
    filename: (_req, file, cb) => {
      const ext = path.extname(file.originalname) || ".jpg";
      cb(null, `${Date.now()}-${Math.random().toString(36).slice(2)}${ext}`);
    },
  }),
  limits: { fileSize: 10 * 1024 * 1024 },
  fileFilter: (_req, file, cb) => {
    if (!file.mimetype.startsWith("image/")) {
      return cb(new Error("only image uploads are allowed"));
    }
    cb(null, true);
  },
});

export const uploadsRouter = Router();

// Serves the check-in photo capture: the mobile app POSTs the JPEG here
// first, then passes the returned URL into POST /checkins.
uploadsRouter.post("/", upload.single("photo"), (req, res) => {
  if (!req.file) return res.status(400).json({ error: "no photo uploaded" });
  res.status(201).json({ photoUrl: `/uploads/${req.file.filename}` });
});
