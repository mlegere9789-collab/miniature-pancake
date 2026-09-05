-- CreateEnum
CREATE TYPE "FlagSeverity" AS ENUM ('COSMETIC', 'MODERATE', 'URGENT');

-- CreateEnum
CREATE TYPE "FlagUrgency" AS ENUM ('MONITOR', 'THIS_WEEK', 'TREAT_TODAY');

-- CreateEnum
CREATE TYPE "FlagStatus" AS ENUM ('OPEN', 'MONITORING', 'RESOLVED');

-- CreateEnum
CREATE TYPE "FlagTrend" AS ENUM ('NEW', 'WORSENING', 'STABLE', 'RESOLVING');

-- CreateTable
CREATE TABLE "User" (
    "id" TEXT NOT NULL,
    "name" TEXT NOT NULL,
    "email" TEXT NOT NULL,
    "location" TEXT,
    "usdaZone" TEXT,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "User_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "Garden" (
    "id" TEXT NOT NULL,
    "userId" TEXT NOT NULL,
    "name" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "latitude" DOUBLE PRECISION,
    "longitude" DOUBLE PRECISION,
    "leaderboardOptIn" BOOLEAN NOT NULL DEFAULT false,
    "yardMapPhotoUrl" TEXT,
    "scoreCurrent" DOUBLE PRECISION NOT NULL DEFAULT 100,

    CONSTRAINT "Garden_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "GardenScoreSnapshot" (
    "id" TEXT NOT NULL,
    "gardenId" TEXT NOT NULL,
    "score" DOUBLE PRECISION NOT NULL,
    "computedAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "GardenScoreSnapshot_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "Plant" (
    "id" TEXT NOT NULL,
    "gardenId" TEXT NOT NULL,
    "speciesId" TEXT NOT NULL,
    "speciesName" TEXT NOT NULL,
    "nickname" TEXT,
    "plantedDate" TIMESTAMP(3),
    "locationPin" JSONB,
    "importanceWeight" DOUBLE PRECISION NOT NULL DEFAULT 1.0,
    "checkinCadenceDays" INTEGER NOT NULL DEFAULT 14,
    "active" BOOLEAN NOT NULL DEFAULT true,
    "frostSensitive" BOOLEAN NOT NULL DEFAULT false,
    "dormancyMonths" INTEGER[] DEFAULT ARRAY[]::INTEGER[],
    "scoreCurrent" DOUBLE PRECISION NOT NULL DEFAULT 100,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "Plant_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "PlantScoreSnapshot" (
    "id" TEXT NOT NULL,
    "plantId" TEXT NOT NULL,
    "score" DOUBLE PRECISION NOT NULL,
    "computedAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "PlantScoreSnapshot_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "CheckIn" (
    "id" TEXT NOT NULL,
    "plantId" TEXT NOT NULL,
    "timestamp" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "photoUrl" TEXT NOT NULL,
    "engineOutputJson" JSONB NOT NULL,
    "computedScore" DOUBLE PRECISION NOT NULL,
    "subscoreBreakdownJson" JSONB NOT NULL,

    CONSTRAINT "CheckIn_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "DiagnosticFlag" (
    "id" TEXT NOT NULL,
    "checkinId" TEXT NOT NULL,
    "condition" TEXT NOT NULL,
    "confidence" DOUBLE PRECISION NOT NULL,
    "severity" "FlagSeverity" NOT NULL,
    "urgency" "FlagUrgency" NOT NULL,
    "trend" "FlagTrend" NOT NULL DEFAULT 'NEW',
    "status" "FlagStatus" NOT NULL DEFAULT 'OPEN',

    CONSTRAINT "DiagnosticFlag_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "TreatmentPlan" (
    "id" TEXT NOT NULL,
    "diagnosticFlagId" TEXT NOT NULL,
    "steps" JSONB NOT NULL,
    "productsRecommended" JSONB NOT NULL,
    "completed" BOOLEAN NOT NULL DEFAULT false,
    "outcomeScoreDelta" DOUBLE PRECISION,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "TreatmentPlan_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE UNIQUE INDEX "User_email_key" ON "User"("email");

-- CreateIndex
CREATE INDEX "GardenScoreSnapshot_gardenId_computedAt_idx" ON "GardenScoreSnapshot"("gardenId", "computedAt");

-- CreateIndex
CREATE INDEX "PlantScoreSnapshot_plantId_computedAt_idx" ON "PlantScoreSnapshot"("plantId", "computedAt");

-- CreateIndex
CREATE INDEX "CheckIn_plantId_timestamp_idx" ON "CheckIn"("plantId", "timestamp");

-- CreateIndex
CREATE UNIQUE INDEX "TreatmentPlan_diagnosticFlagId_key" ON "TreatmentPlan"("diagnosticFlagId");

-- AddForeignKey
ALTER TABLE "Garden" ADD CONSTRAINT "Garden_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "GardenScoreSnapshot" ADD CONSTRAINT "GardenScoreSnapshot_gardenId_fkey" FOREIGN KEY ("gardenId") REFERENCES "Garden"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "Plant" ADD CONSTRAINT "Plant_gardenId_fkey" FOREIGN KEY ("gardenId") REFERENCES "Garden"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "PlantScoreSnapshot" ADD CONSTRAINT "PlantScoreSnapshot_plantId_fkey" FOREIGN KEY ("plantId") REFERENCES "Plant"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "CheckIn" ADD CONSTRAINT "CheckIn_plantId_fkey" FOREIGN KEY ("plantId") REFERENCES "Plant"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "DiagnosticFlag" ADD CONSTRAINT "DiagnosticFlag_checkinId_fkey" FOREIGN KEY ("checkinId") REFERENCES "CheckIn"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TreatmentPlan" ADD CONSTRAINT "TreatmentPlan_diagnosticFlagId_fkey" FOREIGN KEY ("diagnosticFlagId") REFERENCES "DiagnosticFlag"("id") ON DELETE RESTRICT ON UPDATE CASCADE;
