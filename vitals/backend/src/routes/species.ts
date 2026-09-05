import { Router } from "express";
import { searchSpecies } from "../services/speciesDormancy";

export const speciesRouter = Router();

// Species autocomplete for the Add Plant screen: substring-matches the
// curated dormancy table so the user can pick a recognized species and get
// its dormancy habit right, instead of free-typing something that never
// matches.
speciesRouter.get("/search", (req, res) => {
  const q = typeof req.query.q === "string" ? req.query.q : "";
  res.json(searchSpecies(q));
});
