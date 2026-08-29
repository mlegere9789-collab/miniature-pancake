import { z } from "zod";

export const CreatePlantSchema = z.object({
  gardenId: z.string().uuid(),
  speciesId: z.string().min(1),
  speciesName: z.string().min(1),
  nickname: z.string().optional(),
  plantedDate: z.coerce.date().optional(),
  locationPin: z.object({ x: z.number(), y: z.number() }).optional(),
  importanceWeight: z.number().min(0).max(10).default(1),
  checkinCadenceDays: z.number().int().min(1).max(365).default(14),
  frostSensitive: z.boolean().default(false),
  dormancyMonths: z.array(z.number().int().min(1).max(12)).default([]),
});

export type CreatePlantInput = z.infer<typeof CreatePlantSchema>;
