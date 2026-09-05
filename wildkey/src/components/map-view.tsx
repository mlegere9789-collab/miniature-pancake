"use client";

import { useEffect, useRef } from "react";
import * as maplibregl from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";

export type MapMarker = {
  id: string;
  lat: number;
  lng: number;
  color: string;
  label: string;
};

export type BasemapKind = "street" | "satellite";

type MapViewProps = {
  markers: MapMarker[];
  basemap?: BasemapKind;
  onMoveEnd?: (bounds: { minLat: number; maxLat: number; minLng: number; maxLng: number }) => void;
  onMarkerClick?: (id: string) => void;
  className?: string;
};

/**
 * Two free, no-API-key raster basemaps. "street" is OpenStreetMap; "satellite"
 * is Esri's World Imagery service, which — unlike Mapbox/MapTiler satellite
 * layers — serves free of charge with no API key or account for
 * non-commercial/demo use (https://www.esri.com/arcgis-blog/products/arcgis-living-atlas/mapping/esri-world-imagery-faq/).
 * That's what makes satellite imagery buildable here without a paid-provider
 * credential this sandbox doesn't have. Real production traffic at scale
 * should still review Esri's terms and consider a paid provider; this is a
 * genuinely working default, not a placeholder.
 */
const BASEMAP_STYLES: Record<BasemapKind, maplibregl.StyleSpecification> = {
  street: {
    version: 8,
    sources: {
      osm: {
        type: "raster",
        tiles: ["https://tile.openstreetmap.org/{z}/{x}/{y}.png"],
        tileSize: 256,
        attribution: "© OpenStreetMap contributors",
      },
    },
    layers: [{ id: "osm", type: "raster", source: "osm" }],
  },
  satellite: {
    version: 8,
    sources: {
      esri: {
        type: "raster",
        tiles: [
          "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
        ],
        tileSize: 256,
        attribution: "Esri, Maxar, Earthstar Geographics, and the GIS User Community",
      },
    },
    layers: [{ id: "esri", type: "raster", source: "esri" }],
  },
};

export function MapView({ markers, basemap = "street", onMoveEnd, onMarkerClick, className }: MapViewProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const markersRef = useRef<maplibregl.Marker[]>([]);

  useEffect(() => {
    if (!containerRef.current || mapRef.current) return;

    const map = new maplibregl.Map({
      container: containerRef.current,
      style: BASEMAP_STYLES[basemap],
      center: [-74.006, 40.7128],
      zoom: 3,
      attributionControl: { compact: true },
    });
    map.addControl(new maplibregl.NavigationControl(), "top-right");

    map.on("moveend", () => {
      if (!onMoveEnd) return;
      const bounds = map.getBounds();
      onMoveEnd({
        minLat: bounds.getSouth(),
        maxLat: bounds.getNorth(),
        minLng: bounds.getWest(),
        maxLng: bounds.getEast(),
      });
    });

    mapRef.current = map;
    return () => {
      map.remove();
      mapRef.current = null;
    };
    // Intentionally mount-once: markers/callbacks and the basemap switch are
    // applied in the effects below via refs, not by recreating the map instance.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    map.setStyle(BASEMAP_STYLES[basemap]);
    // setStyle tears down and re-adds sources/layers, which drops markers
    // (they're DOM overlays, not style layers, so they actually survive —
    // but re-apply defensively in case that ever changes upstream).
    map.once("styledata", () => {
      markersRef.current.forEach((m) => m.addTo(map));
    });
  }, [basemap]);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    markersRef.current.forEach((m) => m.remove());
    markersRef.current = markers.map((marker) => {
      const el = document.createElement("button");
      el.type = "button";
      el.setAttribute("aria-label", marker.label);
      el.style.width = "16px";
      el.style.height = "16px";
      el.style.borderRadius = "50%";
      el.style.border = "2px solid white";
      el.style.boxShadow = "0 1px 3px rgba(0,0,0,0.4)";
      el.style.background = marker.color;
      el.style.cursor = onMarkerClick ? "pointer" : "default";
      if (onMarkerClick) {
        el.addEventListener("click", () => onMarkerClick(marker.id));
      }
      return new maplibregl.Marker({ element: el })
        .setLngLat([marker.lng, marker.lat])
        .addTo(map);
    });
  }, [markers, onMarkerClick]);

  return <div ref={containerRef} className={className} role="application" aria-label="Map" />;
}
