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

type MapViewProps = {
  markers: MapMarker[];
  onMoveEnd?: (bounds: { minLat: number; maxLat: number; minLng: number; maxLng: number }) => void;
  onMarkerClick?: (id: string) => void;
  className?: string;
};

/**
 * Free, no-API-key raster basemap: OpenStreetMap tiles, attribution
 * required by their tile usage policy. Satellite-default (Part E.2) would
 * need a paid provider (Mapbox, MapTiler) and an API key — swap the
 * `sources.osm.tiles` URL and add a key when that's available; nothing
 * else about this component needs to change (see
 * docs/remaining-systems-design.md).
 */
const OSM_STYLE: maplibregl.StyleSpecification = {
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
};

export function MapView({ markers, onMoveEnd, onMarkerClick, className }: MapViewProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const markersRef = useRef<maplibregl.Marker[]>([]);

  useEffect(() => {
    if (!containerRef.current || mapRef.current) return;

    const map = new maplibregl.Map({
      container: containerRef.current,
      style: OSM_STYLE,
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
    // Intentionally mount-once: markers/callbacks are applied in the
    // effects below via refs, not by recreating the map instance.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

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
