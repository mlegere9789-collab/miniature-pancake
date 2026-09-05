import { z } from "zod";

export const CreateCheckInSchema = z.object({
  plantId: z.string().uuid(),
  photoUrl: z.string().min(1),
});

export type CreateCheckInInput = z.infer<typeof CreateCheckInSchema>;
