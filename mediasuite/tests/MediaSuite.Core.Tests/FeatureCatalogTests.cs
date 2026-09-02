using MediaSuite.Core.Features;
using Xunit;

namespace MediaSuite.Core.Tests;

public class FeatureCatalogTests
{
    [Fact]
    public void Operation_ids_are_unique()
    {
        var duplicates = FeatureCatalog.All
            .GroupBy(f => f.OperationId, StringComparer.OrdinalIgnoreCase)
            .Where(group => group.Count() > 1)
            .Select(group => group.Key)
            .ToList();

        Assert.Empty(duplicates);
    }

    [Theory]
    [InlineData(FeatureSection.Convert, 38)]
    [InlineData(FeatureSection.Compress, 8)]
    [InlineData(FeatureSection.Tools, 21)]
    [InlineData(FeatureSection.Upscale, 1)]
    public void Every_feature_from_the_brief_is_present(FeatureSection section, int expectedCount)
    {
        // These counts come straight from the brief's feature list. If a feature is added
        // or dropped, this test is the reminder to check it against the agreed scope.
        Assert.Equal(expectedCount, FeatureCatalog.InSection(section).Count());
    }

    [Fact]
    public void Descriptors_are_fully_populated()
    {
        foreach (var feature in FeatureCatalog.All)
        {
            Assert.False(string.IsNullOrWhiteSpace(feature.Name), feature.OperationId);
            Assert.False(string.IsNullOrWhiteSpace(feature.Group), feature.OperationId);
            Assert.False(string.IsNullOrWhiteSpace(feature.Description), feature.OperationId);
            Assert.InRange(feature.BuildStep, 1, 18);
            Assert.Contains('.', feature.OperationId);
        }
    }

    [Fact]
    public void FromOperationId_is_case_insensitive_and_null_for_unknown()
    {
        Assert.NotNull(FeatureCatalog.FromOperationId("PDF.MERGE"));
        Assert.Null(FeatureCatalog.FromOperationId("pdf.does-not-exist"));
    }

    [Fact]
    public void Grouping_preserves_every_feature_in_a_section()
    {
        foreach (var section in Enum.GetValues<FeatureSection>())
        {
            var grouped = FeatureCatalog.GroupedBySection(section).SelectMany(g => g).Count();
            Assert.Equal(FeatureCatalog.InSection(section).Count(), grouped);
        }
    }
}
